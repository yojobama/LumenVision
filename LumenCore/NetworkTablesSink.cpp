#ifdef LUMEN_WITH_NT4
#include "NetworkTablesSink.h"
#include "CoordinateFrames.h"
#include "ResultPacket.h"
#include "SystemMonitor.h"
#include <opencv2/geometry/2d.hpp> // cv::minAreaRect (OpenCV 5 moved it out of imgproc)
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <string_view>

namespace {
	// arbitrary but generous; a real deployment binds a handful of detector nodes, not dozens
	constexpr int MAX_BOUND_SOURCES = 16;

	// Shoelace formula on the 4 corners; needs no calibration, so it can pick a "best" target on an uncalibrated camera.
	double QuadPixelArea(const std::array<float, 8>& c) {
		double sum = 0.0;
		for (int i = 0; i < 4; i++) {
			int j = (i + 1) % 4;
			sum += static_cast<double>(c[i * 2]) * c[j * 2 + 1] - static_cast<double>(c[j * 2]) * c[i * 2 + 1];
		}
		return std::abs(sum) / 2.0;
	}

	// The detector's "pose"/"altPose" object ({x, y, z, R}) holds libapriltag's raw camera-to-tag pose; this returns it in WPILib's
	// frames (camera X forward / Y left / Z up, tag frame as in a field layout), or false when the object is absent.
	bool PoseFromJson(const nlohmann::json& pose, frames::Pose3& wpilib) {
		if (!pose.is_object()) return false;
		frames::Pose3 openCv;
		openCv.t = { pose.value("x", 0.0), pose.value("y", 0.0), pose.value("z", 0.0) };
		if (pose.contains("R")) {
			for (int row = 0; row < 3; row++)
				for (int col = 0; col < 3; col++) openCv.R[row * 3 + col] = pose["R"][row][col].get<double>();
		}
		wpilib = frames::AprilTagPoseToWpilib(openCv);
		return true;
	}

	void SetPose(const frames::Pose3& pose, std::array<float, 3>& t, std::array<float, 4>& q) {
		t = { static_cast<float>(pose.t[0]), static_cast<float>(pose.t[1]), static_cast<float>(pose.t[2]) };
		std::array<double, 4> quaternion = frames::RotationToQuaternion(pose.R);
		q = { static_cast<float>(quaternion[0]), static_cast<float>(quaternion[1]), static_cast<float>(quaternion[2]), static_cast<float>(quaternion[3]) };
	}

	// Minimum-area rectangle around the four corners, as x0,y0..x3,y3; `skewDeg` is its rotation angle.
	void FillMinAreaRect(PacketTarget& t) {
		std::vector<cv::Point2f> points;
		for (int i = 0; i < 4; i++) points.emplace_back(t.corners[i * 2], t.corners[i * 2 + 1]);
		cv::RotatedRect rect = cv::minAreaRect(points);
		cv::Point2f box[4];
		rect.points(box);
		for (int i = 0; i < 4; i++) {
			t.minAreaRectCorners[i * 2] = box[i].x;
			t.minAreaRectCorners[i * 2 + 1] = box[i].y;
		}
		t.skewDeg = rect.angle;
	}

	// `poses` (optional) receives each tag's WPILib-frame camera-to-tag pose, or an identity pose for a tag without one, in the same order as the targets.
	std::vector<PacketTarget> ComputeTagTargets(const nlohmann::json& tagsArray, const nlohmann::json& calibration,
		std::vector<frames::Pose3>* poses = nullptr) {
		bool hasFrameSize = calibration.is_object() && calibration.value("imageWidth", 0) > 0 && calibration.value("imageHeight", 0) > 0;
		double frameArea = hasFrameSize ? static_cast<double>(calibration.value("imageWidth", 0)) * calibration.value("imageHeight", 0) : 0.0;

		std::vector<PacketTarget> out;
		out.reserve(tagsArray.size());
		for (const auto& tag : tagsArray) {
			PacketTarget m;
			m.fiducialId = static_cast<int16_t>(tag.value("id", -1));
			const auto& corners = tag["corners"];
			for (int i = 0; i < 4; i++) {
				m.corners[i * 2] = static_cast<float>(corners[i][0].get<double>());
				m.corners[i * 2 + 1] = static_cast<float>(corners[i][1].get<double>());
			}
			double pixelArea = QuadPixelArea(m.corners);
			m.areaPercent = hasFrameSize ? (pixelArea / frameArea) * 100.0 : 0.0;
			FillMinAreaRect(m);

			frames::Pose3 bestPose;
			if (tag.contains("pose") && PoseFromJson(tag["pose"], bestPose)) {
				m.hasPose = true;
				SetPose(bestPose, m.bestT, m.bestQ);
				// PhotonLib's conventions: yaw is the horizontal angle off boresight, positive to the LEFT (standard maths, as the camera
				// frame's Y axis); pitch is the vertical angle, positive up.
				m.yawDeg = std::atan2(bestPose.t[1], bestPose.t[0]) * 180.0 / std::numbers::pi;
				m.pitchDeg = std::atan2(bestPose.t[2], bestPose.t[0]) * 180.0 / std::numbers::pi;
				m.bestReprojErr = static_cast<float>(tag.value("reprojErr", -1.0));
				m.poseAmbiguity = tag.value("poseAmbiguity", -1.0);
				frames::Pose3 altPose;
				if (tag.contains("altPose") && PoseFromJson(tag["altPose"], altPose)) {
					SetPose(altPose, m.altT, m.altQ);
					m.altReprojErr = static_cast<float>(tag.value("altReprojErr", -1.0));
				}
			}
			if (poses != nullptr) poses->push_back(bestPose);
			out.push_back(m);
		}
		return out;
	}

	// ObjectDetectionSink entries: {classId, className, confidence, box:[x,y,w,h], frameWidth?, frameHeight?, yawDeg?, pitchDeg?}
	std::vector<PacketTarget> ComputeObjectTargets(const nlohmann::json& detections) {
		std::vector<PacketTarget> out;
		out.reserve(detections.size());
		for (const auto& detection : detections) {
			PacketTarget m;
			m.objectClassId = static_cast<int16_t>(detection.value("classId", -1));
			m.objectConfidence = static_cast<float>(detection.value("confidence", -1.0));
			m.yawDeg = detection.value("yawDeg", 0.0);
			m.pitchDeg = detection.value("pitchDeg", 0.0);

			const auto& box = detection["box"];
			double x = box[0].get<double>(), y = box[1].get<double>(), w = box[2].get<double>(), h = box[3].get<double>();
			m.corners = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + w), static_cast<float>(y),
				static_cast<float>(x + w), static_cast<float>(y + h), static_cast<float>(x), static_cast<float>(y + h) };
			m.minAreaRectCorners = m.corners;
			double frameArea = static_cast<double>(detection.value("frameWidth", 0)) * detection.value("frameHeight", 0);
			m.areaPercent = frameArea > 0.0 ? (w * h) / frameArea * 100.0 : 0.0;
			out.push_back(m);
		}
		return out;
	}

	// NT table names are path segments: anything outside this set would split or confuse a topic path
	std::string SanitiseTableName(const std::string& name) {
		std::string out = name;
		for (char& c : out) {
			bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
			if (!ok) c = '_';
		}
		return out;
	}
}

NetworkTablesSink::NetworkTablesSink(std::shared_ptr<Logger> logger, std::string id, NetworkTablesConfig config, SystemMonitor* systemMonitor)
	: ISink(logger, MAX_BOUND_SOURCES, true /* requireJson */, false /* requireFrame */, id)
	, m_Instance(nt::NetworkTableInstance::Create())
	, m_Config(config)
	, m_Logger(logger)
	, m_SystemMonitor(systemMonitor)
	, m_ConstructedAtUs(SourceResult::NowUs())
{
	if (m_Logger) m_Logger->EnterLog("NetworkTablesSink constructed, identity=" + config.clientIdentity);

	if (config.teamNumber.has_value()) {
		m_Instance.SetServerTeam(config.teamNumber.value(), config.port);
	} else {
		m_Instance.SetServer(config.serverAddress, config.port);
	}
	m_Instance.StartClient4(config.clientIdentity);

	// prefix-subscribed to this sink's whole subtree (not per bound source) so a robot can write
	// "<rootTable>/<sourceId>/config/pipelineIndex" or ".../driverMode" for any source, bound or not.
	// The leading slash is required: topic names are absolute, and a prefix without it matches nothing.
	std::string configPrefix = "/" + m_Config.rootTable + "/";
	std::array<std::string_view, 1> prefixes{ std::string_view(configPrefix) };
	m_ConfigListener = m_Instance.AddListener(prefixes, NT_EVENT_VALUE_REMOTE,
		[this](const nt::Event& event) { OnConfigValueChanged(event); });

	m_RecordingStatusPublisher = m_Instance.GetBooleanTopic("/" + m_Config.rootTable + "/status/recording").Publish();
	m_RecordingStatusPublisher.Set(false);
	m_LedStatusPublisher = m_Instance.GetIntegerTopic("/" + m_Config.rootTable + "/status/ledMode").Publish();
	m_LedStatusPublisher.Set(-1);
}

NetworkTablesSink::~NetworkTablesSink()
{
	m_Instance.RemoveListener(m_ConfigListener);
	m_Instance.StopClient();
}

void NetworkTablesSink::OnConfigValueChanged(const nt::Event& event)
{
	const nt::ValueEventData* valueData = event.GetValueEventData();
	if (valueData == nullptr) return;

	// "/<rootTable>/<sourceId>/config/pipelineIndex" or ".../driverMode"; GetTopicName returns a leading-slash absolute name, so the prefix includes it
	std::string name = nt::GetTopicName(valueData->topic);
	std::string prefix = "/" + m_Config.rootTable + "/";
	if (name.rfind(prefix, 0) != 0) return;
	std::string rest = name.substr(prefix.size());

	// coprocessor-wide, not per-source: "/<rootTable>/config/recording" - the per-source parse
	// below requires exactly "<id>/config/<leaf>" and would drop it
	if (rest == "config/recording") {
		if (valueData->value.IsBoolean()) {
			std::lock_guard<std::mutex> lock(m_ConfigMutex);
			m_PendingRecording = valueData->value.GetBoolean();
		}
		return;
	}
	if (rest == "config/ledMode") {
		if (valueData->value.IsInteger() || valueData->value.IsDouble()) {
			std::lock_guard<std::mutex> lock(m_ConfigMutex);
			m_PendingLed = valueData->value.IsInteger()
				? static_cast<int>(valueData->value.GetInteger())
				: static_cast<int>(valueData->value.GetDouble());
		}
		return;
	}

	size_t firstSlash = rest.find('/');
	size_t secondSlash = rest.find('/', firstSlash == std::string::npos ? std::string::npos : firstSlash + 1);
	if (firstSlash == std::string::npos || secondSlash == std::string::npos) return;
	if (rest.substr(firstSlash + 1, secondSlash - firstSlash - 1) != "config") return;

	// the topic path carries the camera's table name (its alias, when one is set); requests are keyed by node id
	std::string sourceId = NodeIdForTableName(rest.substr(0, firstSlash));
	std::string leaf = rest.substr(secondSlash + 1);

	std::lock_guard<std::mutex> lock(m_ConfigMutex);
	if (leaf == "pipelineIndex" && (valueData->value.IsInteger() || valueData->value.IsDouble())) {
		m_PendingConfig[sourceId].pipelineIndex = valueData->value.IsInteger()
			? static_cast<int>(valueData->value.GetInteger())
			: static_cast<int>(valueData->value.GetDouble());
	} else if (leaf == "driverMode" && valueData->value.IsBoolean()) {
		m_PendingConfig[sourceId].driverMode = valueData->value.GetBoolean();
	} else if (leaf == "fpsLimit" && (valueData->value.IsInteger() || valueData->value.IsDouble())) {
		m_PendingConfig[sourceId].fpsLimit = valueData->value.IsInteger()
			? static_cast<int>(valueData->value.GetInteger())
			: static_cast<int>(valueData->value.GetDouble());
	} else if (leaf == "inputSnapshot") {
		m_PendingConfig[sourceId].inputSnapshots++;
	} else if (leaf == "outputSnapshot") {
		m_PendingConfig[sourceId].outputSnapshots++;
	}
}

std::string NetworkTablesSink::PollConfigRequests()
{
	std::unordered_map<std::string, PendingConfigRequest> drained;
	{
		std::lock_guard<std::mutex> lock(m_ConfigMutex);
		drained.swap(m_PendingConfig);
	}

	nlohmann::json out = nlohmann::json::array();
	for (const auto& [sourceId, request] : drained) {
		nlohmann::json entry{ {"sourceId", sourceId} };
		if (request.pipelineIndex.has_value()) entry["pipelineIndex"] = request.pipelineIndex.value();
		if (request.driverMode.has_value()) entry["driverMode"] = request.driverMode.value();
		if (request.fpsLimit.has_value()) entry["fpsLimit"] = request.fpsLimit.value();
		if (request.inputSnapshots > 0) entry["inputSnapshots"] = request.inputSnapshots;
		if (request.outputSnapshots > 0) entry["outputSnapshots"] = request.outputSnapshots;
		out.push_back(entry);
	}
	return out.dump();
}

int NetworkTablesSink::PollRecordingRequest()
{
	std::lock_guard<std::mutex> lock(m_ConfigMutex);
	if (!m_PendingRecording.has_value()) return -1;
	int value = m_PendingRecording.value() ? 1 : 0;
	m_PendingRecording.reset();
	return value;
}

void NetworkTablesSink::SetRecordingStatus(bool recording)
{
	m_RecordingStatusPublisher.Set(recording);
}

int NetworkTablesSink::PollLedRequest()
{
	std::lock_guard<std::mutex> lock(m_ConfigMutex);
	if (!m_PendingLed.has_value()) return -2;
	int value = m_PendingLed.value();
	m_PendingLed.reset();
	return value;
}

void NetworkTablesSink::SetLedStatus(int mode)
{
	m_LedStatusPublisher.Set(mode);
}

void NetworkTablesSink::PublishNodeStatus(const std::string& nodeId, int pipelineIndex, bool driverMode, int fpsLimit)
{
	auto table = m_Instance.GetTable(m_Config.rootTable + "/" + TableNameForNode(nodeId));
	table->PutNumber("status/pipelineIndex", pipelineIndex);
	table->PutBoolean("status/driverMode", driverMode);
	table->PutNumber("status/fpsLimit", fpsLimit);
}

bool NetworkTablesSink::IsConnected() const
{
	return m_Instance.IsConnected();
}

std::string NetworkTablesSink::GetConnectionStatus() const
{
	nlohmann::json status{
		{"connected", IsConnected()},
		{"identity", m_Config.clientIdentity},
		{"rootTable", m_Config.rootTable},
	};
	if (m_Config.teamNumber.has_value()) {
		status["teamNumber"] = m_Config.teamNumber.value();
	} else {
		status["serverAddress"] = m_Config.serverAddress;
	}
	return status.dump();
}

void NetworkTablesSink::SetNodeAlias(const std::string& nodeId, const std::string& alias)
{
	std::lock_guard<std::mutex> lock(m_AliasMutex);
	auto existing = m_NodeAliases.find(nodeId);
	if (existing != m_NodeAliases.end()) {
		m_AliasToNode.erase(existing->second);
		m_NodeAliases.erase(existing);
	}
	std::string sanitised = SanitiseTableName(alias);
	if (sanitised.empty()) return;
	m_NodeAliases[nodeId] = sanitised;
	m_AliasToNode[sanitised] = nodeId;
}

std::string NetworkTablesSink::TableNameForNode(const std::string& nodeId) const
{
	std::lock_guard<std::mutex> lock(m_AliasMutex);
	auto it = m_NodeAliases.find(nodeId);
	return it != m_NodeAliases.end() ? it->second : nodeId;
}

std::string NetworkTablesSink::NodeIdForTableName(const std::string& tableName) const
{
	std::lock_guard<std::mutex> lock(m_AliasMutex);
	auto it = m_AliasToNode.find(tableName);
	return it != m_AliasToNode.end() ? it->second : tableName;
}

void NetworkTablesSink::PublishSourceResult(const SourceResult& result)
{
	const std::string& sourceId = result.sourceId;
	const nlohmann::json& json = result.json.value();
	auto table = m_Instance.GetTable(m_Config.rootTable + "/" + TableNameForNode(sourceId));
	NetworkTablesStreamStats& stats = m_StreamStats[sourceId];

	// Every per-camera topic is published with the frame's capture time as its NT timestamp, so a robot reading it (or its
	// readQueue) gets the time the image was taken. ntcore converts this client-local time to the server's time base itself.
	// SourceResult times are wall-clock; only their difference (the frame's age) is used, so the two clocks never mix.
	const int64_t publishNtUs = nt::Now();
	const int64_t ageUs = static_cast<int64_t>(SourceResult::NowUs()) - static_cast<int64_t>(result.captureTimeUs);
	int64_t captureNtUs = publishNtUs - std::clamp<int64_t>(ageUs, 0, publishNtUs);
	captureNtUs = std::max(captureNtUs, stats.lastNtCaptureUs + 1); // a topic's timestamps must not go backwards
	stats.lastNtCaptureUs = captureNtUs;

	auto putBoolean = [&](const std::string& key, bool v) { table->GetEntry(key).SetValue(nt::Value::MakeBoolean(v, captureNtUs)); };
	auto putNumber = [&](const std::string& key, double v) { table->GetEntry(key).SetValue(nt::Value::MakeDouble(v, captureNtUs)); };
	auto putNumberArray = [&](const std::string& key, const std::vector<double>& v) {
		table->GetEntry(key).SetValue(nt::Value::MakeDoubleArray(v, captureNtUs));
	};
	auto putString = [&](const std::string& key, const std::string& v) { table->GetEntry(key).SetValue(nt::Value::MakeString(v, captureNtUs)); };
	auto putRaw = [&](const std::string& key, const std::vector<uint8_t>& v) { table->GetEntry(key).SetValue(nt::Value::MakeRaw(v, captureNtUs)); };

	// AprilTag detector shape: a bare array of {id, center, corners, pose:{x,y,z,R}} objects, or an object envelope
	// {"tags": [...], "multiTag": {...} | null, "calibration": {...} | null}; ObjectDetectionSink publishes a bare array of
	// {classId, confidence, box, ...}. Detected structurally, as no "type" field exists.
	nlohmann::json tagsArray;
	nlohmann::json calibration = nullptr;
	bool looksLikeTags = false;
	bool looksLikeObjects = false;
	bool hasEnvelope = false;
	if (json.is_array()) {
		// An EMPTY array counts too, so tags leaving frame update to "zero tags" instead of leaving stale values published.
		looksLikeObjects = !json.empty() && json[0].is_object() && json[0].contains("classId") && json[0].contains("box");
		looksLikeTags = !looksLikeObjects && (json.empty() || (json[0].is_object() && json[0].contains("id") && json[0].contains("pose")));
		tagsArray = json;
	} else if (json.is_object() && json.contains("tags") && json["tags"].is_array()) {
		looksLikeTags = true;
		hasEnvelope = true;
		tagsArray = json["tags"];
		calibration = json.value("calibration", nlohmann::json(nullptr));
	}

	std::vector<PacketTarget> targets;
	PacketHeader header;
	header.sequenceId = result.frameNumber;

	if (looksLikeTags) {
		std::vector<double> ids, x, y, z;
		// row-major 3x3 rotation matrix flattened into 9 parallel arrays (r0..r8, ApriltagDetector.cpp's R layout): NT4 has no nested
		// arrays, and the raw matrix lets WPILib's Rotation3d(Matrix<N3, N3>) build a Transform3d robot-side.
		std::array<std::vector<double>, 9> r;
		ids.reserve(tagsArray.size());
		x.reserve(tagsArray.size());
		y.reserve(tagsArray.size());
		z.reserve(tagsArray.size());
		for (auto& ri : r) ri.reserve(tagsArray.size());

		std::vector<frames::Pose3> tagPoses;
		targets = ComputeTagTargets(tagsArray, calibration, &tagPoses);
		for (size_t i = 0; i < targets.size(); i++) {
			// tags/x,y,z and r0..r8 hold the camera-to-tag pose in WPILib frames ("pose" is only present once the detector has a
			// calibration; otherwise they keep their zero/identity defaults)
			ids.push_back(tagsArray[i].value("id", -1));
			x.push_back(tagPoses[i].t[0]);
			y.push_back(tagPoses[i].t[1]);
			z.push_back(tagPoses[i].t[2]);
			for (int k = 0; k < 9; k++) r[k].push_back(tagPoses[i].R[k]);
		}

		putNumberArray("tags/ids", ids);
		putNumberArray("tags/x", x);
		putNumberArray("tags/y", y);
		putNumberArray("tags/z", z);
		for (int i = 0; i < 9; i++) {
			putNumberArray("tags/r" + std::to_string(i), r[i]);
		}

		// multi-tag PnP result, only present in the object-envelope shape. Published as scalars (one per frame); x/y/z/r0..r8 are the
		// CAMERA's pose in FIELD frame, not camera-to-tag as in tags/x,y,z.
		nlohmann::json multiTag = hasEnvelope ? json.value("multiTag", nlohmann::json(nullptr)) : nlohmann::json(nullptr);
		if (!multiTag.is_null()) {
			putNumber("multitag/x", multiTag.value("x", 0.0));
			putNumber("multitag/y", multiTag.value("y", 0.0));
			putNumber("multitag/z", multiTag.value("z", 0.0));
			static const std::array<double, 3> identityRow{ 0.0, 0.0, 0.0 };
			auto rotation = multiTag.value("R", nlohmann::json::array({identityRow, identityRow, identityRow}));
			for (int row = 0; row < 3; row++) {
				for (int col = 0; col < 3; col++) {
					putNumber("multitag/r" + std::to_string(row * 3 + col), rotation[row][col].get<double>());
				}
			}
			putNumber("multitag/tagCount", multiTag.value("tagCount", 0));
			putNumber("multitag/reprojErrPixels", multiTag.value("reprojErrPixels", 0.0));
			for (int id : multiTag.value("fiducialIds", std::vector<int>{})) header.multiTagIds.push_back(static_cast<uint16_t>(id));
		} else {
			// no multi-tag result this frame (fewer than 2 known-field-pose tags, no field layout, or no envelope): clear tagCount to 0
			// rather than leaving a stale pose published.
			putNumber("multitag/tagCount", 0);
		}

		if (calibration.is_object()) {
			double fx = calibration.value("fx", 0.0), fy = calibration.value("fy", 0.0);
			double cx = calibration.value("cx", 0.0), cy = calibration.value("cy", 0.0);
			putNumberArray("cameraIntrinsics", std::vector<double>{fx, 0, cx, 0, fy, cy, 0, 0, 1});
			putNumberArray("cameraDistortion", calibration.value("distCoeffs", std::vector<double>{}));
		}
	} else if (looksLikeObjects) {
		targets = ComputeObjectTargets(json);
		// the object list stays readable as JSON for consumers that want class names
		putString("raw", json.dump());
	} else {
		putString("raw", json.dump());
	}

	if (looksLikeTags || looksLikeObjects) {
		// --- flattened best-target scalars + versioned binary packet ---
		const PacketTarget* best = nullptr;
		for (const auto& t : targets) {
			if (best == nullptr || t.areaPercent > best->areaPercent) best = &t;
		}

		putBoolean("hasTargets", best != nullptr);
		putNumber("targetYaw", best ? best->yawDeg : 0.0);
		putNumber("targetPitch", best ? best->pitchDeg : 0.0);
		putNumber("targetArea", best ? best->areaPercent : 0.0);
		// [x, y, z, qw, qx, qy, qz]: translation + unit quaternion as a flat double array (no wpi::Struct<Transform3d> specialisation here)
		putNumberArray("targetPose", best
			? std::vector<double>{best->bestT[0], best->bestT[1], best->bestT[2], best->bestQ[0], best->bestQ[1], best->bestQ[2], best->bestQ[3]}
			: std::vector<double>{0, 0, 0, 1, 0, 0, 0});

		// pipeline latency (capture -> published-to-NT), independent of NT4's network timestamping
		header.latencyUs = static_cast<uint32_t>(std::clamp<int64_t>(ageUs, 0, std::numeric_limits<uint32_t>::max()));
		putRaw("result", BuildResultPacket(header, targets));
	}

	double latencyMs = static_cast<double>(std::max<int64_t>(ageUs, 0)) / 1000.0;
	putNumber("latencyMs", latencyMs);

	if (stats.lastCaptureTimeUs != 0 && result.captureTimeUs > stats.lastCaptureTimeUs && result.frameNumber > stats.lastFrameNumber) {
		double deltaSeconds = static_cast<double>(result.captureTimeUs - stats.lastCaptureTimeUs) / 1'000'000.0;
		double deltaFrames = static_cast<double>(result.frameNumber - stats.lastFrameNumber);
		if (deltaSeconds > 0.0) stats.fps = deltaFrames / deltaSeconds;
	}
	stats.lastCaptureTimeUs = result.captureTimeUs;
	stats.lastFrameNumber = result.frameNumber;
	putNumber("fps", stats.fps);
}

void NetworkTablesSink::Process(const std::vector<SourceResult>& results)
{
	for (const SourceResult& result : results) {
		if (!result.json.has_value()) continue;
		PublishSourceResult(result);
	}

	auto rootTable = m_Instance.GetTable(m_Config.rootTable);
	rootTable->PutNumber("heartbeat", static_cast<double>(m_Heartbeat++));

	// re-published every tick: a value Set() before the NT4 client first connects doesn't reliably reach the server (as with heartbeat/.status)
	rootTable->PutString(".version", LUMEN_VERSION_STRING);

	// server time offset (nt::GetServerTimeOffset), surfaced as-is so a robot program can tell when its addVisionMeasurement() timestamps can't be trusted yet
	std::optional<int64_t> serverTimeOffsetUs = m_Instance.GetServerTimeOffset();
	nlohmann::json statusJson{
		{"uptimeSeconds", static_cast<double>(SourceResult::NowUs() - m_ConstructedAtUs) / 1'000'000.0},
		{"nodeCount", static_cast<int>(results.size())},
		{"serverTimeOffsetUs", serverTimeOffsetUs.has_value() ? nlohmann::json(serverTimeOffsetUs.value()) : nlohmann::json(nullptr)},
	};
	if (m_SystemMonitor != nullptr) {
		statusJson["cpuUsage"] = m_SystemMonitor->GetCPUUsage();
		statusJson["cpuTemperature"] = m_SystemMonitor->GetCPUTemperature();
		statusJson["ramUsage"] = m_SystemMonitor->GetRAMUsage();
	}
	rootTable->PutString(".status", statusJson.dump());
}

#endif // LUMEN_WITH_NT4
