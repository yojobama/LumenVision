#ifdef LUMEN_WITH_NT4
#include "NetworkTablesSink.h"
#include "SystemMonitor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <string_view>

namespace {
	// arbitrary but generous; a real deployment binds a handful of detector nodes, not dozens
	constexpr int MAX_BOUND_SOURCES = 16;

	// bumped whenever the binary "result" packet layout changes; a consumer refuses payloads from a schema version it doesn't understand
	constexpr uint16_t RESULT_PACKET_SCHEMA_VERSION = 1;

	void AppendU8(std::vector<uint8_t>& buf, uint8_t v) {
		buf.push_back(v);
	}
	void AppendU16(std::vector<uint8_t>& buf, uint16_t v) {
		buf.push_back(static_cast<uint8_t>(v >> 8));
		buf.push_back(static_cast<uint8_t>(v));
	}
	// big-endian regardless of host, as the packet layout is an explicit contract
	void AppendF32(std::vector<uint8_t>& buf, float v) {
		uint32_t bits;
		static_assert(sizeof(bits) == sizeof(v));
		std::memcpy(&bits, &v, sizeof(bits));
		for (int shift = 24; shift >= 0; shift -= 8) buf.push_back(static_cast<uint8_t>(bits >> shift));
	}
	void AppendF64(std::vector<uint8_t>& buf, double v) {
		uint64_t bits;
		static_assert(sizeof(bits) == sizeof(v));
		std::memcpy(&bits, &v, sizeof(bits));
		for (int shift = 56; shift >= 0; shift -= 8) buf.push_back(static_cast<uint8_t>(bits >> shift));
	}

	// One tag's worth of everything both the binary packet and the flattened scalar topics need,
	// computed once per tag rather than duplicated between the two publish paths below.
	struct TargetMetrics {
		int id = -1;
		double yawDeg = 0.0;
		double pitchDeg = 0.0;
		// always 0: PhotonVision's AprilTag pipelines never compute skew (it only applies to colour-shape pipelines)
		double skewDeg = 0.0;
		double areaPercent = 0.0;
		bool hasPose = false;
		float tx = 0.0f, ty = 0.0f, tz = 0.0f;
		float qw = 1.0f, qx = 0.0f, qy = 0.0f, qz = 0.0f;
		// always 0: estimate_tag_pose is single-hypothesis, so there is no second pose to derive an ambiguity from; kept so the layout matches
		double poseAmbiguity = 0.0;
		std::array<float, 8> corners{}; // x0,y0,x1,y1,x2,y2,x3,y3
	};

	// Shoelace formula on the 4 corners; needs no calibration, so it can pick a "best" target on an uncalibrated camera.
	double QuadPixelArea(const std::array<float, 8>& c) {
		double sum = 0.0;
		for (int i = 0; i < 4; i++) {
			int j = (i + 1) % 4;
			sum += static_cast<double>(c[i * 2]) * c[j * 2 + 1] - static_cast<double>(c[j * 2]) * c[i * 2 + 1];
		}
		return std::abs(sum) / 2.0;
	}

	// Row-major 3x3 rotation matrix (ApriltagDetector.cpp's pose.R layout) to unit quaternion, via the largest-diagonal-term
	// method (stable for small trace).
	void RotationMatrixToQuaternion(const nlohmann::json& r, float& qw, float& qx, float& qy, float& qz) {
		double m00 = r[0][0], m01 = r[0][1], m02 = r[0][2];
		double m10 = r[1][0], m11 = r[1][1], m12 = r[1][2];
		double m20 = r[2][0], m21 = r[2][1], m22 = r[2][2];
		double trace = m00 + m11 + m22;
		double w, x, y, z;
		if (trace > 0.0) {
			double s = std::sqrt(trace + 1.0) * 2.0;
			w = 0.25 * s;
			x = (m21 - m12) / s;
			y = (m02 - m20) / s;
			z = (m10 - m01) / s;
		} else if (m00 > m11 && m00 > m22) {
			double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
			w = (m21 - m12) / s;
			x = 0.25 * s;
			y = (m01 + m10) / s;
			z = (m02 + m20) / s;
		} else if (m11 > m22) {
			double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
			w = (m02 - m20) / s;
			x = (m01 + m10) / s;
			y = 0.25 * s;
			z = (m12 + m21) / s;
		} else {
			double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
			w = (m10 - m01) / s;
			x = (m02 + m20) / s;
			y = (m12 + m21) / s;
			z = 0.25 * s;
		}
		qw = static_cast<float>(w);
		qx = static_cast<float>(x);
		qy = static_cast<float>(y);
		qz = static_cast<float>(z);
	}

	std::vector<TargetMetrics> ComputeTargetMetrics(const nlohmann::json& tagsArray, const nlohmann::json& calibration) {
		bool hasFrameSize = calibration.is_object() && calibration.value("imageWidth", 0) > 0 && calibration.value("imageHeight", 0) > 0;
		double frameArea = hasFrameSize ? static_cast<double>(calibration.value("imageWidth", 0)) * calibration.value("imageHeight", 0) : 0.0;

		std::vector<TargetMetrics> out;
		out.reserve(tagsArray.size());
		for (const auto& tag : tagsArray) {
			TargetMetrics m;
			m.id = tag.value("id", -1);
			const auto& corners = tag["corners"];
			for (int i = 0; i < 4; i++) {
				m.corners[i * 2] = static_cast<float>(corners[i][0].get<double>());
				m.corners[i * 2 + 1] = static_cast<float>(corners[i][1].get<double>());
			}
			double pixelArea = QuadPixelArea(m.corners);
			m.areaPercent = hasFrameSize ? (pixelArea / frameArea) * 100.0 : 0.0;

			if (tag.contains("pose")) {
				const auto& pose = tag["pose"];
				double x = pose.value("x", 0.0), y = pose.value("y", 0.0), z = pose.value("z", 0.0);
				m.hasPose = true;
				m.tx = static_cast<float>(x);
				m.ty = static_cast<float>(y);
				m.tz = static_cast<float>(z);
				// apriltag camera-frame convention (also used for tags/x,y,z): +X right, +Y down, +Z forward. Yaw is the horizontal angle
				// off boresight (positive = right), pitch the vertical angle (Y negated: positive = above, as PhotonVision). Not remapped
				// to WPILib NWU; the robot-side vendordep applies its own conversion.
				m.yawDeg = std::atan2(x, z) * 180.0 / std::numbers::pi;
				m.pitchDeg = std::atan2(-y, z) * 180.0 / std::numbers::pi;
				static const std::array<double, 3> identityRow{ 0.0, 0.0, 0.0 };
				auto rotation = pose.value("R", nlohmann::json::array({identityRow, identityRow, identityRow}));
				RotationMatrixToQuaternion(rotation, m.qw, m.qx, m.qy, m.qz);
			}
			out.push_back(m);
		}
		return out;
	}

	// [u16 schemaVersion][u8 targetCount][repeated per target: u16 fiducialId, f64 yaw, f64 pitch, f64 area, f64 skew, f32x3
	// translation, f32x4 rotation-quaternion(w,x,y,z), f64 poseAmbiguity, f32x8 corners], all big-endian (see AppendU16/F32/F64).
	// Hand-packed like photonlib's Packet as results are variable-length; this comment is the spec the Java decoder follows.
	std::vector<uint8_t> BuildResultPacket(const std::vector<TargetMetrics>& targets) {
		std::vector<uint8_t> packet;
		AppendU16(packet, RESULT_PACKET_SCHEMA_VERSION);
		AppendU8(packet, static_cast<uint8_t>(std::min<size_t>(targets.size(), 255)));
		for (size_t i = 0; i < targets.size() && i < 255; i++) {
			const TargetMetrics& t = targets[i];
			AppendU16(packet, static_cast<uint16_t>(t.id));
			AppendF64(packet, t.yawDeg);
			AppendF64(packet, t.pitchDeg);
			AppendF64(packet, t.areaPercent);
			AppendF64(packet, t.skewDeg);
			AppendF32(packet, t.tx);
			AppendF32(packet, t.ty);
			AppendF32(packet, t.tz);
			AppendF32(packet, t.qw);
			AppendF32(packet, t.qx);
			AppendF32(packet, t.qy);
			AppendF32(packet, t.qz);
			AppendF64(packet, t.poseAmbiguity);
			for (float c : t.corners) AppendF32(packet, c);
		}
		return packet;
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

	size_t firstSlash = rest.find('/');
	size_t secondSlash = rest.find('/', firstSlash == std::string::npos ? std::string::npos : firstSlash + 1);
	if (firstSlash == std::string::npos || secondSlash == std::string::npos) return;
	if (rest.substr(firstSlash + 1, secondSlash - firstSlash - 1) != "config") return;

	std::string sourceId = rest.substr(0, firstSlash);
	std::string leaf = rest.substr(secondSlash + 1);

	std::lock_guard<std::mutex> lock(m_ConfigMutex);
	if (leaf == "pipelineIndex" && (valueData->value.IsInteger() || valueData->value.IsDouble())) {
		m_PendingConfig[sourceId].pipelineIndex = valueData->value.IsInteger()
			? static_cast<int>(valueData->value.GetInteger())
			: static_cast<int>(valueData->value.GetDouble());
	} else if (leaf == "driverMode" && valueData->value.IsBoolean()) {
		m_PendingConfig[sourceId].driverMode = valueData->value.GetBoolean();
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

void NetworkTablesSink::PublishSourceResult(const SourceResult& result)
{
	const std::string& sourceId = result.sourceId;
	const nlohmann::json& json = result.json.value();
	auto table = m_Instance.GetTable(m_Config.rootTable + "/" + sourceId);

	// AprilTag detector shape: a bare array of {id, center, corners, pose:{x,y,z,R}} objects, or an object envelope
	// {"tags": [...], "multiTag": {...} | null, "calibration": {...} | null}. Detected structurally, as no "type" field exists.
	nlohmann::json tagsArray;
	nlohmann::json calibration = nullptr;
	bool looksLikeTags = false;
	bool hasEnvelope = false;
	if (json.is_array()) {
		// An EMPTY array counts too, so tags leaving frame update to "zero tags" instead of leaving stale values published.
		looksLikeTags = json.empty() || (json[0].is_object() && json[0].contains("id") && json[0].contains("pose"));
		tagsArray = json;
	} else if (json.is_object() && json.contains("tags") && json["tags"].is_array()) {
		looksLikeTags = true;
		hasEnvelope = true;
		tagsArray = json["tags"];
		calibration = json.value("calibration", nlohmann::json(nullptr));
	}

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

		for (const auto& tag : tagsArray) {
			ids.push_back(tag.value("id", -1));
			// "pose" is only present when ApriltagDetector had a calibration (m_HasCalibration); otherwise tags publish ids/corners only,
			// leaving x/y/z/R at their zeroed defaults.
			nlohmann::json pose = tag.value("pose", nlohmann::json::object());
			x.push_back(pose.value("x", 0.0));
			y.push_back(pose.value("y", 0.0));
			z.push_back(pose.value("z", 0.0));

			static const std::array<double, 3> identityRow{ 0.0, 0.0, 0.0 };
			auto rotation = pose.value("R", nlohmann::json::array({identityRow, identityRow, identityRow}));
			for (int row = 0; row < 3; row++) {
				for (int col = 0; col < 3; col++) {
					r[row * 3 + col].push_back(rotation[row][col].get<double>());
				}
			}
		}

		table->PutNumberArray("tags/ids", ids);
		table->PutNumberArray("tags/x", x);
		table->PutNumberArray("tags/y", y);
		table->PutNumberArray("tags/z", z);
		for (int i = 0; i < 9; i++) {
			table->PutNumberArray("tags/r" + std::to_string(i), r[i]);
		}

		// multi-tag PnP result, only present in the object-envelope shape. Published as scalars (one per frame); x/y/z/r0..r8 are the
		// CAMERA's pose in FIELD frame, not camera-to-tag as in tags/x,y,z.
		nlohmann::json multiTag = hasEnvelope ? json.value("multiTag", nlohmann::json(nullptr)) : nlohmann::json(nullptr);
		if (!multiTag.is_null()) {
			table->PutNumber("multitag/x", multiTag.value("x", 0.0));
			table->PutNumber("multitag/y", multiTag.value("y", 0.0));
			table->PutNumber("multitag/z", multiTag.value("z", 0.0));
			static const std::array<double, 3> identityRow{ 0.0, 0.0, 0.0 };
			auto rotation = multiTag.value("R", nlohmann::json::array({identityRow, identityRow, identityRow}));
			for (int row = 0; row < 3; row++) {
				for (int col = 0; col < 3; col++) {
					table->PutNumber("multitag/r" + std::to_string(row * 3 + col), rotation[row][col].get<double>());
				}
			}
			table->PutNumber("multitag/tagCount", multiTag.value("tagCount", 0));
			table->PutNumber("multitag/reprojErrPixels", multiTag.value("reprojErrPixels", 0.0));
		} else {
			// no multi-tag result this frame (fewer than 2 known-field-pose tags, no field layout, or no envelope): clear tagCount to 0
			// rather than leaving a stale pose published.
			table->PutNumber("multitag/tagCount", 0);
		}

		// --- flattened best-target scalars + versioned binary packet ---
		std::vector<TargetMetrics> targets = ComputeTargetMetrics(tagsArray, calibration);
		const TargetMetrics* best = nullptr;
		for (const auto& t : targets) {
			if (best == nullptr || t.areaPercent > best->areaPercent) best = &t;
		}

		table->PutBoolean("hasTargets", best != nullptr);
		table->PutNumber("targetYaw", best ? best->yawDeg : 0.0);
		table->PutNumber("targetPitch", best ? best->pitchDeg : 0.0);
		table->PutNumber("targetArea", best ? best->areaPercent : 0.0);
		// [x, y, z, qw, qx, qy, qz]: translation + unit quaternion as a flat double array (no wpi::Struct<Transform3d> specialisation here)
		table->PutNumberArray("targetPose", best
			? std::vector<double>{best->tx, best->ty, best->tz, best->qw, best->qx, best->qy, best->qz}
			: std::vector<double>{0, 0, 0, 1, 0, 0, 0});

		if (calibration.is_object()) {
			double fx = calibration.value("fx", 0.0), fy = calibration.value("fy", 0.0);
			double cx = calibration.value("cx", 0.0), cy = calibration.value("cy", 0.0);
			table->PutNumberArray("cameraIntrinsics", std::vector<double>{fx, 0, cx, 0, fy, cy, 0, 0, 1});
			table->PutNumberArray("cameraDistortion", calibration.value("distCoeffs", std::vector<double>{}));
		}

		table->PutRaw("result", BuildResultPacket(targets));
	} else {
		table->PutString("raw", json.dump());
	}

	// pipeline latency (capture -> published-to-NT), independent of NT4's network timestamping; both times come from SourceResult::NowUs()
	double latencyMs = (result.producedTimeUs > result.captureTimeUs)
		? static_cast<double>(result.producedTimeUs - result.captureTimeUs) / 1000.0
		: 0.0;
	table->PutNumber("latencyMs", latencyMs);

	NetworkTablesStreamStats& stats = m_StreamStats[sourceId];
	if (stats.lastCaptureTimeUs != 0 && result.captureTimeUs > stats.lastCaptureTimeUs && result.frameNumber > stats.lastFrameNumber) {
		double deltaSeconds = static_cast<double>(result.captureTimeUs - stats.lastCaptureTimeUs) / 1'000'000.0;
		double deltaFrames = static_cast<double>(result.frameNumber - stats.lastFrameNumber);
		if (deltaSeconds > 0.0) stats.fps = deltaFrames / deltaSeconds;
	}
	stats.lastCaptureTimeUs = result.captureTimeUs;
	stats.lastFrameNumber = result.frameNumber;
	table->PutNumber("fps", stats.fps);
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
