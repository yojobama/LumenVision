#include <catch2/catch_test_macros.hpp>
#include "ApriltagDetector.h"
#include "CameraCalibrationResult.h"
#include "NetworkTablesSink.h"
#include "ObjectDetectionSink.h"
#include "ResultPacket.h"
#include "SystemMonitor.h"
#include <networktables/RawTopic.h>
#include <chrono>
#include <thread>

// End-to-end NT4 schema check: ImageFileSource -> ApriltagDetector -> NetworkTablesSink on the
// vkapriltag sample image, read back by a second ntcore instance over loopback (no calibration,
// so pose-dependent fields are not checked).

namespace {
class PgmFrameSource : public ISource {
public:
	PgmFrameSource(std::shared_ptr<Logger> logger, std::string id, cv::Mat bgrFrame)
		: ISource(logger, id), m_Frame(std::move(bgrFrame))
	{
	}

protected:
	void CaptureFrame() override {
		SetLatestResult(SourceResult(std::nullopt, m_Frame.clone()));
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

private:
	cv::Mat m_Frame;
};

// An object detector's output without a model: the same detections every frame, and the calibration it reports to a sink that asks.
class FakeObjectDetector : public ISource {
public:
	FakeObjectDetector(std::shared_ptr<Logger> logger, std::string id, nlohmann::json detections, std::optional<CameraCalibrationResult> calibration)
		: ISource(logger, id), m_Detections(std::move(detections)), m_Calibration(std::move(calibration))
	{
	}

	std::optional<CameraCalibrationResult> GetCalibration() override { return m_Calibration; }

protected:
	void CaptureFrame() override {
		SetLatestResult(SourceResult(m_Detections, std::nullopt));
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

private:
	nlohmann::json m_Detections;
	std::optional<CameraCalibrationResult> m_Calibration;
};

// Polls until the condition holds or a timeout expires.
template<typename Predicate>
bool WaitUntil(Predicate predicate, int maxAttempts = 100, int delayMs = 50) {
	for (int attempt = 0; attempt < maxAttempts; attempt++) {
		if (predicate()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
	}
	return predicate();
}
}

TEST_CASE("NetworkTablesSink publishes the real NT4 schema end to end over a loopback server", "[nt4][e2e]") {
	const std::string pgmPath = std::string(LUMEN_VKAPRILTAG_SAMPLE_DIR) + "/grayimage.pgm";
	cv::Mat gray = cv::imread(pgmPath, cv::IMREAD_GRAYSCALE);
	REQUIRE_FALSE(gray.empty());
	cv::Mat bgr;
	cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);

	// Isolated non-default ports (distinct for NT3 and NT4) and no persistence file.
	constexpr unsigned int TEST_NT3_PORT = 17809;
	constexpr unsigned int TEST_NT4_PORT = 17810;

	nt::NetworkTableInstance server = nt::NetworkTableInstance::Create();
	server.StartServer("", "127.0.0.1", TEST_NT3_PORT, TEST_NT4_PORT);

	auto logger = std::make_shared<Logger>("LumenCoreTests-nt4-e2e.log");
	auto imageSource = std::make_shared<PgmFrameSource>(logger, "nt4-e2e-image", bgr);
	auto detector = std::make_shared<ApriltagDetector>(logger, "nt4-e2e-detector", CameraCalibrationResult(), 0.1651);
	REQUIRE(detector->BindSource(imageSource));

	NetworkTablesConfig config;
	config.serverAddress = "127.0.0.1";
	config.port = TEST_NT4_PORT;
	config.rootTable = "lumenvision";
	config.clientIdentity = "LumenCoreTests-nt4-e2e";
	auto ntSink = std::make_shared<NetworkTablesSink>(logger, "nt4-e2e-sink", config);
	REQUIRE(ntSink->BindSource(detector));

	imageSource->Toggle(true);
	static_cast<ISink&>(*detector).Toggle(true);
	static_cast<ISink&>(*ntSink).Toggle(true);

	nt::NetworkTableInstance client = nt::NetworkTableInstance::Create();
	client.SetServer("127.0.0.1", TEST_NT4_PORT);
	client.StartClient4("LumenCoreTests-nt4-e2e-reader");
	auto clientTable = client.GetTable("lumenvision");
	auto detectorTable = clientTable->GetSubTable("nt4-e2e-detector");

	REQUIRE(WaitUntil([&] { return clientTable->GetNumber("heartbeat", -1.0) >= 0.0; }));
	REQUIRE(WaitUntil([&] { return clientTable->GetString(".version", "") == LUMEN_VERSION_STRING; }));
	REQUIRE(WaitUntil([&] { return detectorTable->GetBoolean("hasTargets", false); }));
	REQUIRE(WaitUntil([&] { return !clientTable->GetString(".status", "").empty(); }));
	// "result" is the last topic written per source, so wait for it too.
	REQUIRE(WaitUntil([&] { return detectorTable->GetRaw("result", std::vector<uint8_t>{}).size() >= 3; }));
	// First read of "latencyMs" needs a wait for the new subscription to sync.
	REQUIRE(WaitUntil([&] { return detectorTable->GetNumber("latencyMs", -1.0) >= 0.0; }));

	imageSource->Toggle(false);
	static_cast<ISink&>(*detector).Toggle(false);
	static_cast<ISink&>(*ntSink).Toggle(false);

	REQUIRE(detectorTable->GetNumber("latencyMs", -1.0) >= 0.0);
	// the result packet is the one encoding of the tags: no parallel arrays or multi-tag scalars next to it
	REQUIRE_FALSE(detectorTable->GetEntry("tags/ids").Exists());
	REQUIRE_FALSE(detectorTable->GetEntry("multitag/tagCount").Exists());

	std::vector<uint8_t> packet = detectorTable->GetRaw("result", std::vector<uint8_t>{});
	PacketHeader packetHeader;
	std::vector<PacketTarget> packetTargets;
	REQUIRE(ParseResultPacket(packet, packetHeader, packetTargets)); // schema version 2
	REQUIRE_FALSE(packetTargets.empty());
	REQUIRE(packetTargets[0].fiducialId >= 0);
	REQUIRE(packetHeader.sequenceId > 0);

	std::string status = clientTable->GetString(".status", "");
	nlohmann::json statusJson = nlohmann::json::parse(status, nullptr, false /* allow_exceptions */);
	REQUIRE_FALSE(statusJson.is_discarded());
	REQUIRE(statusJson.contains("uptimeSeconds"));

	client.StopClient();
	server.StopServer();
	nt::NetworkTableInstance::Destroy(client);
	nt::NetworkTableInstance::Destroy(server);
}

TEST_CASE("NetworkTablesSink surfaces robot-writable per-node config writes keyed by node id", "[nt4]") {
	constexpr unsigned int TEST_NT3_PORT = 17811;
	constexpr unsigned int TEST_NT4_PORT = 17812;

	nt::NetworkTableInstance server = nt::NetworkTableInstance::Create();
	server.StartServer("", "127.0.0.1", TEST_NT3_PORT, TEST_NT4_PORT);

	auto logger = std::make_shared<Logger>("LumenCoreTests-nt4-config.log");
	NetworkTablesConfig config;
	config.serverAddress = "127.0.0.1";
	config.port = TEST_NT4_PORT;
	config.rootTable = "lumenvision";
	config.clientIdentity = "LumenCoreTests-nt4-config-sink";
	auto ntSink = std::make_shared<NetworkTablesSink>(logger, "nt4-config-sink", config);

	// No Process() tick runs; PollConfigRequests works from the listener callback alone.
	REQUIRE(ntSink->PollConfigRequests() == "[]");

	nt::NetworkTableInstance robot = nt::NetworkTableInstance::Create();
	robot.SetServer("127.0.0.1", TEST_NT4_PORT);
	robot.StartClient4("LumenCoreTests-nt4-config-robot");
	auto robotSourceTable = robot.GetTable("lumenvision/some-detector-id");
	robotSourceTable->PutBoolean("config/enabled", false);
	robotSourceTable->PutNumber("config/fpsLimit", 12);

	nlohmann::json requests;
	REQUIRE(WaitUntil([&] {
		requests = nlohmann::json::parse(ntSink->PollConfigRequests(), nullptr, false);
		return !requests.is_discarded() && !requests.empty();
	}));
	REQUIRE(requests.size() == 1);
	REQUIRE(requests[0]["sourceId"] == "some-detector-id");
	REQUIRE(requests[0]["enabled"] == false);
	REQUIRE(requests[0]["fpsLimit"] == 12);

	// Requests are consumed on poll.
	REQUIRE(ntSink->PollConfigRequests() == "[]");

	robot.StopClient();
	server.StopServer();
	nt::NetworkTableInstance::Destroy(robot);
	nt::NetworkTableInstance::Destroy(server);
}

TEST_CASE("NetworkTablesSink surfaces the robot's config/recording request and publishes status/recording", "[nt4]") {
	constexpr unsigned int TEST_NT3_PORT = 17821;
	constexpr unsigned int TEST_NT4_PORT = 17822;

	nt::NetworkTableInstance server = nt::NetworkTableInstance::Create();
	server.StartServer("", "127.0.0.1", TEST_NT3_PORT, TEST_NT4_PORT);

	auto logger = std::make_shared<Logger>("LumenCoreTests-nt4-recording.log");
	NetworkTablesConfig config;
	config.serverAddress = "127.0.0.1";
	config.port = TEST_NT4_PORT;
	config.rootTable = "lumenvision";
	config.clientIdentity = "LumenCoreTests-nt4-recording-sink";
	auto ntSink = std::make_shared<NetworkTablesSink>(logger, "nt4-recording-sink", config);

	REQUIRE(ntSink->PollRecordingRequest() == -1); // nothing written yet

	nt::NetworkTableInstance robot = nt::NetworkTableInstance::Create();
	robot.SetServer("127.0.0.1", TEST_NT4_PORT);
	robot.StartClient4("LumenCoreTests-nt4-recording-robot");
	// As published by lumenlib LumenCoprocessor.setRecording().
	auto recordingPub = robot.GetBooleanTopic("/lumenvision/config/recording").Publish();
	recordingPub.Set(true);

	int request = -1;
	REQUIRE(WaitUntil([&] { request = ntSink->PollRecordingRequest(); return request != -1; }));
	REQUIRE(request == 1);
	REQUIRE(ntSink->PollRecordingRequest() == -1); // consumed
	// a coprocessor-wide topic must not leak into the per-source config array
	REQUIRE(ntSink->PollConfigRequests() == "[]");

	recordingPub.Set(false);
	REQUIRE(WaitUntil([&] { request = ntSink->PollRecordingRequest(); return request != -1; }));
	REQUIRE(request == 0);

	// Read by LumenCoprocessor.isRecording().
	auto statusSub = robot.GetBooleanTopic("/lumenvision/status/recording").Subscribe(false);
	ntSink->SetRecordingStatus(true);
	REQUIRE(WaitUntil([&] { return statusSub.Get() == true; }));
	ntSink->SetRecordingStatus(false);
	REQUIRE(WaitUntil([&] { return statusSub.Get() == false; }));

	robot.StopClient();
	server.StopServer();
	nt::NetworkTableInstance::Destroy(robot);
	nt::NetworkTableInstance::Destroy(server);
}

TEST_CASE("NetworkTablesSink publishes both pose solutions, ambiguity and capture-time stamps for a calibrated detector", "[nt4][e2e]") {
	cv::Mat gray = cv::imread(std::string(LUMEN_VKAPRILTAG_SAMPLE_DIR) + "/grayimage.pgm", cv::IMREAD_GRAYSCALE);
	REQUIRE_FALSE(gray.empty());
	cv::Mat bgr;
	cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);

	constexpr unsigned int TEST_NT3_PORT = 17831;
	constexpr unsigned int TEST_NT4_PORT = 17832;
	nt::NetworkTableInstance server = nt::NetworkTableInstance::Create();
	server.StartServer("", "127.0.0.1", TEST_NT3_PORT, TEST_NT4_PORT);

	auto logger = std::make_shared<Logger>("LumenCoreTests-nt4-pose.log");
	auto imageSource = std::make_shared<PgmFrameSource>(logger, "nt4-pose-image", bgr);
	CameraCalibrationResult calibration(1000.0, 1000.0, gray.cols / 2.0, gray.rows / 2.0, 0.1, std::vector<double>{}, gray.cols, gray.rows);
	auto detector = std::make_shared<ApriltagDetector>(logger, "nt4-pose-detector", calibration, 0.1651);
	REQUIRE(detector->BindSource(imageSource));

	NetworkTablesConfig config;
	config.serverAddress = "127.0.0.1";
	config.port = TEST_NT4_PORT;
	config.rootTable = "lumenvision";
	config.clientIdentity = "LumenCoreTests-nt4-pose-sink";
	auto ntSink = std::make_shared<NetworkTablesSink>(logger, "nt4-pose-sink", config);
	// robot code addresses the camera by name, not by its node id
	ntSink->SetNodeAlias("nt4-pose-detector", "front camera");
	REQUIRE(ntSink->BindSource(detector));

	imageSource->Toggle(true);
	static_cast<ISink&>(*detector).Toggle(true);
	static_cast<ISink&>(*ntSink).Toggle(true);

	nt::NetworkTableInstance client = nt::NetworkTableInstance::Create();
	client.SetServer("127.0.0.1", TEST_NT4_PORT);
	client.StartClient4("LumenCoreTests-nt4-pose-reader");
	nt::RawSubscriber resultSub = client.GetRawTopic("/lumenvision/front_camera/result").Subscribe("raw", {});

	std::vector<uint8_t> packet;
	int64_t valueTimeUs = 0;
	REQUIRE(WaitUntil([&] {
		nt::TimestampedRaw sample = resultSub.GetAtomic();
		if (sample.value.size() < 3) return false;
		packet = sample.value;
		valueTimeUs = sample.time;
		return true;
	}));
	int64_t readAtUs = client.GetServerTimeOffset().has_value() ? nt::Now() + client.GetServerTimeOffset().value() : nt::Now();

	imageSource->Toggle(false);
	static_cast<ISink&>(*detector).Toggle(false);
	static_cast<ISink&>(*ntSink).Toggle(false);

	PacketHeader header;
	std::vector<PacketTarget> targets;
	REQUIRE(ParseResultPacket(packet, header, targets));
	REQUIRE_FALSE(targets.empty());
	const PacketTarget& tag = targets[0];
	REQUIRE(tag.hasPose);
	REQUIRE(tag.bestReprojErr >= 0.0f);
	REQUIRE(tag.poseAmbiguity >= 0.0);
	REQUIRE(tag.poseAmbiguity <= 1.0);
	// the planar-PnP second solution is present whenever the tag is not exactly fronto-parallel
	if (tag.altReprojErr >= 0.0f) REQUIRE(tag.altReprojErr >= tag.bestReprojErr - 1e-3f);
	REQUIRE(tag.areaPercent > 0.0);
	REQUIRE(header.sequenceId > 0);

	// the NT value timestamp is the frame's capture time, so it is not in the future and is older than the packet's own latency allows
	REQUIRE(valueTimeUs > 0);
	REQUIRE(valueTimeUs <= readAtUs);

	client.StopClient();
	server.StopServer();
	nt::NetworkTableInstance::Destroy(client);
	nt::NetworkTableInstance::Destroy(server);
}

TEST_CASE("NetworkTablesSink surfaces the robot's fps, snapshot and LED requests and publishes status read-backs", "[nt4]") {
	constexpr unsigned int TEST_NT3_PORT = 17841;
	constexpr unsigned int TEST_NT4_PORT = 17842;

	nt::NetworkTableInstance server = nt::NetworkTableInstance::Create();
	server.StartServer("", "127.0.0.1", TEST_NT3_PORT, TEST_NT4_PORT);

	auto logger = std::make_shared<Logger>("LumenCoreTests-nt4-controls.log");
	NetworkTablesConfig config;
	config.serverAddress = "127.0.0.1";
	config.port = TEST_NT4_PORT;
	config.rootTable = "lumenvision";
	config.clientIdentity = "LumenCoreTests-nt4-controls-sink";
	auto ntSink = std::make_shared<NetworkTablesSink>(logger, "nt4-controls-sink", config);
	ntSink->SetNodeAlias("node-7", "front");

	REQUIRE(ntSink->PollLedRequest() == -2);

	nt::NetworkTableInstance robot = nt::NetworkTableInstance::Create();
	robot.SetServer("127.0.0.1", TEST_NT4_PORT);
	robot.StartClient4("LumenCoreTests-nt4-controls-robot");
	auto front = robot.GetTable("lumenvision/front");
	front->PutNumber("config/fpsLimit", 15);
	front->PutNumber("config/snapshot", 1);
	front->PutNumber("config/inputSnapshot", 1); // a retired topic: ignored
	robot.GetTable("lumenvision")->PutNumber("config/ledMode", 2);

	nlohmann::json requests;
	REQUIRE(WaitUntil([&] {
		requests = nlohmann::json::parse(ntSink->PollConfigRequests(), nullptr, false);
		return !requests.is_discarded() && !requests.empty() && requests[0].size() >= 3;
	}));
	// the robot addressed the camera by name; the request comes back keyed by the node id behind it
	REQUIRE(requests[0]["sourceId"] == "node-7");
	REQUIRE(requests[0]["fpsLimit"] == 15);
	REQUIRE(requests[0]["snapshots"] == 1);
	REQUIRE_FALSE(requests[0].contains("inputSnapshots"));

	int led = -2;
	REQUIRE(WaitUntil([&] { led = ntSink->PollLedRequest(); return led != -2; }));
	REQUIRE(led == 2);
	REQUIRE(ntSink->PollLedRequest() == -2);

	ntSink->PublishNodeStatus("node-7", 15);
	ntSink->SetLedStatus(2);
	// NT4 has no cross-topic ordering, so each read-back is awaited on its own
	REQUIRE(WaitUntil([&] { return front->GetNumber("status/fpsLimit", -1.0) == 15.0; }));
	REQUIRE(WaitUntil([&] { return robot.GetTable("lumenvision")->GetNumber("status/ledMode", -5.0) == 2.0; }));

	robot.StopClient();
	server.StopServer();
	nt::NetworkTableInstance::Destroy(robot);
	nt::NetworkTableInstance::Destroy(server);
}

TEST_CASE("NetworkTablesSink surfaces per-node enabled and snapshot writes and publishes the graph and protocol", "[nt4]") {
	constexpr unsigned int TEST_NT3_PORT = 17851;
	constexpr unsigned int TEST_NT4_PORT = 17852;

	nt::NetworkTableInstance server = nt::NetworkTableInstance::Create();
	server.StartServer("", "127.0.0.1", TEST_NT3_PORT, TEST_NT4_PORT);

	auto logger = std::make_shared<Logger>("LumenCoreTests-nt4-node-controls.log");
	NetworkTablesConfig config;
	config.serverAddress = "127.0.0.1";
	config.port = TEST_NT4_PORT;
	config.rootTable = "lumenvision";
	config.clientIdentity = "LumenCoreTests-nt4-node-controls-sink";
	auto ntSink = std::make_shared<NetworkTablesSink>(logger, "nt4-node-controls-sink", config);
	ntSink->SetNodeAlias("node-9", "tags");

	nt::NetworkTableInstance robot = nt::NetworkTableInstance::Create();
	robot.SetServer("127.0.0.1", TEST_NT4_PORT);
	robot.StartClient4("LumenCoreTests-nt4-node-controls-robot");
	auto tags = robot.GetTable("lumenvision/tags");
	tags->PutBoolean("config/enabled", false);
	tags->PutBoolean("config/recording", true);
	tags->PutNumber("config/snapshot", 1);

	// ntcore may coalesce back-to-back writes, so one write per poll is checked here
	int snapshots = 0;
	bool sawDisable = false;
	bool sawRecording = false;
	REQUIRE(WaitUntil([&] {
		nlohmann::json requests = nlohmann::json::parse(ntSink->PollConfigRequests(), nullptr, false);
		if (requests.is_discarded()) return false;
		for (const auto& request : requests) {
			REQUIRE(request["sourceId"] == "node-9");
			if (request.contains("enabled") && request["enabled"] == false) sawDisable = true;
			if (request.contains("recording") && request["recording"] == true) sawRecording = true;
			snapshots += request.value("snapshots", 0);
		}
		return sawDisable && sawRecording && snapshots == 1;
	}));
	REQUIRE(ntSink->PollConfigRequests() == "[]");

	ntSink->PublishNodeEnabled("node-9", false);
	ntSink->PublishNodeRecording("node-9", true);
	REQUIRE(WaitUntil([&] { return tags->GetEntry("status/recording").Exists() && tags->GetBoolean("status/recording", false); }));
	REQUIRE(WaitUntil([&] { return tags->GetEntry("status/enabled").Exists() && !tags->GetBoolean("status/enabled", true); }));

	// .graph and .protocol ride on Process(), which runs once a bound source has produced a result
	ntSink->SetGraphJson("{\"nodes\":[]}");
	auto upstream = std::make_shared<PgmFrameSource>(logger, "node-controls-upstream", cv::Mat(48, 64, CV_8UC3, cv::Scalar(0, 0, 0)));
	upstream->Toggle(true);
	REQUIRE(ntSink->BindSource(upstream));
	REQUIRE(ntSink->IsBoundTo(upstream->GetID()));
	REQUIRE_FALSE(ntSink->IsBoundTo("some-other-source"));
	static_cast<ISink&>(*ntSink).Toggle(true);
	auto root = robot.GetTable("lumenvision");
	REQUIRE(WaitUntil([&] { return root->GetNumber(".protocol", -1.0) == kNetworkTablesProtocol; }));
	REQUIRE(WaitUntil([&] { return root->GetString(".graph", "") == "{\"nodes\":[]}"; }));

	static_cast<ISink&>(*ntSink).Toggle(false);
	upstream->Toggle(false);
	robot.StopClient();
	server.StopServer();
	nt::NetworkTableInstance::Destroy(robot);
	nt::NetworkTableInstance::Destroy(server);
}

// A NetworkTablesSink the server rebuilds in place (the NT settings changed) must keep publishing its bound detector's results.
TEST_CASE("a NetworkTablesSink rebuilt under the same id keeps publishing the bound detector's results", "[nt4][e2e][rebuild]") {
	const std::string pgmPath = std::string(LUMEN_VKAPRILTAG_SAMPLE_DIR) + "/grayimage.pgm";
	cv::Mat gray = cv::imread(pgmPath, cv::IMREAD_GRAYSCALE);
	REQUIRE_FALSE(gray.empty());
	cv::Mat bgr;
	cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);

	constexpr unsigned int NT3_PORT = 17851;
	constexpr unsigned int NT4_PORT = 17852;
	nt::NetworkTableInstance server = nt::NetworkTableInstance::Create();
	server.StartServer("", "127.0.0.1", NT3_PORT, NT4_PORT);
	nt::NetworkTableInstance client = nt::NetworkTableInstance::Create();
	client.SetServer("127.0.0.1", NT4_PORT);
	client.StartClient4("LumenCoreTests-rebuild-reader");
	auto clientTable = client.GetTable("lumenvision");
	auto detectorTable = clientTable->GetSubTable("rebuild-detector");

	auto logger = std::make_shared<Logger>("LumenCoreTests-nt4-rebuild.log");
	auto imageSource = std::make_shared<PgmFrameSource>(logger, "rebuild-image", bgr);
	auto detector = std::make_shared<ApriltagDetector>(logger, "rebuild-detector", CameraCalibrationResult(), 0.1651);
	REQUIRE(detector->BindSource(imageSource));

	NetworkTablesConfig config;
	config.serverAddress = "127.0.0.1";
	config.port = NT4_PORT;
	config.rootTable = "lumenvision";
	config.clientIdentity = "rebuild-1";
	auto ntSink = std::make_shared<NetworkTablesSink>(logger, "rebuild-sink", config);
	REQUIRE(ntSink->BindSource(detector));

	imageSource->Toggle(true);
	static_cast<ISink&>(*detector).Toggle(true);
	static_cast<ISink&>(*ntSink).Toggle(true);
	REQUIRE(WaitUntil([&] { return detectorTable->GetBoolean("hasTargets", false); }, 200, 50));

	REQUIRE(WaitUntil([&] { return clientTable->GetString(".status", "").find("uptimeSeconds") != std::string::npos; }, 200, 50));
	const double uptimeBeforeRebuild = nlohmann::json::parse(clientTable->GetString(".status", "{}"), nullptr, false).value("uptimeSeconds", 1e9) + 0.5;

	// rebuilt the way Manager::DeleteSink + CreateNetworkTablesSinkForServer(id, ...) do it: the old sink is toggled off and destroyed first
	static_cast<ISink&>(*ntSink).Toggle(false);
	ntSink.reset();
	config.clientIdentity = "rebuild-2";
	ntSink = std::make_shared<NetworkTablesSink>(logger, "rebuild-sink", config);
	REQUIRE(ntSink->BindSource(detector));
	static_cast<ISink&>(*ntSink).Toggle(true);

	// the rebuilt sink publishes its own ".status" (uptime restarts at zero, nodeCount is how many bound nodes' results it processed), so a status that
	// is younger than the old sink's and counts a node proves the new sink is processing the detector's results
	auto status = [&] { return nlohmann::json::parse(clientTable->GetString(".status", "{}"), nullptr, false); };
	REQUIRE(WaitUntil([&] {
		nlohmann::json s = status();
		return s.is_object() && s.value("uptimeSeconds", 1e9) < uptimeBeforeRebuild && s.value("nodeCount", 0) >= 1;
	}, 200, 50));
	REQUIRE(detectorTable->GetBoolean("hasTargets", false));

	static_cast<ISink&>(*ntSink).Toggle(false);
	static_cast<ISink&>(*detector).Toggle(false);
	imageSource->Toggle(false);
	client.StopClient();
	server.StopServer();
	nt::NetworkTableInstance::Destroy(client);
	nt::NetworkTableInstance::Destroy(server);
}

TEST_CASE("NetworkTablesSink publishes the board's status on its own clock, with no node producing results", "[nt4][e2e]") {
	constexpr unsigned int NT3_PORT = 17861;
	constexpr unsigned int NT4_PORT = 17862;
	nt::NetworkTableInstance server = nt::NetworkTableInstance::Create();
	server.StartServer("", "127.0.0.1", NT3_PORT, NT4_PORT);
	nt::NetworkTableInstance client = nt::NetworkTableInstance::Create();
	client.SetServer("127.0.0.1", NT4_PORT);
	client.StartClient4("status-reader");
	auto clientTable = client.GetTable("lumenvision");

	SystemMonitor monitor(100);
	monitor.StartMonitoring();
	auto logger = std::make_shared<Logger>("LumenCoreTests-nt4-status.log");
	NetworkTablesConfig config;
	config.serverAddress = "127.0.0.1";
	config.port = NT4_PORT;
	config.rootTable = "lumenvision";
	config.clientIdentity = "status-sink";
	// no source is bound: the status must not depend on results arriving
	auto ntSink = std::make_shared<NetworkTablesSink>(logger, "status-sink", config, &monitor);
	static_cast<ISink&>(*ntSink).Toggle(true);

	auto status = [&] { return nlohmann::json::parse(clientTable->GetString(".status", "{}"), nullptr, false); };
	// the monitor's first reading lands after one interval; until then its fields are zero
	REQUIRE(WaitUntil([&] { nlohmann::json s = status(); return s.is_object() && s.contains("throttled") && s.value("uptimeSeconds", 0.0) > 0.3; }));
	nlohmann::json s = status();
	REQUIRE(s.value("nodeCount", -1) == 0);
	REQUIRE(s["temperaturesC"].is_object());
	REQUIRE(s["throttled"].is_boolean());
	REQUIRE((s["cpuTemperatureC"].is_null() || s["cpuTemperatureC"].is_number()));
	for (const char* key : { "cpuUsagePercent", "ramUsedMB", "ramTotalMB", "diskUsedPercent", "diskFreeMB", "boardUptimeSeconds" }) {
		INFO(key);
		REQUIRE(s[key].is_number());
	}
#ifdef __linux__
	REQUIRE(s.value("ramTotalMB", 0.0) > 0.0);
	REQUIRE(s.value("ramUsedMB", 0.0) > 0.0);
	REQUIRE(s.value("ramUsedMB", 0.0) <= s.value("ramTotalMB", 0.0));
	REQUIRE(s.value("diskFreeMB", 0.0) > 0.0);
	REQUIRE(s.value("boardUptimeSeconds", 0.0) > 0.0);
#endif

	// published on a timer: a later read shows a later sink uptime
	const double first = s.value("uptimeSeconds", 0.0);
	REQUIRE(WaitUntil([&] { return status().value("uptimeSeconds", 0.0) > first; }));

	static_cast<ISink&>(*ntSink).Toggle(false);
	ntSink.reset();
	monitor.StopMonitoring();
	client.StopClient();
	server.StopServer();
	nt::NetworkTableInstance::Destroy(client);
	nt::NetworkTableInstance::Destroy(server);
}

TEST_CASE("an object detector's calibration is published as its camera intrinsics, with or without detections", "[nt4][e2e]") {
	constexpr unsigned int NT3_PORT = 17871;
	constexpr unsigned int NT4_PORT = 17872;
	nt::NetworkTableInstance server = nt::NetworkTableInstance::Create();
	server.StartServer("", "127.0.0.1", NT3_PORT, NT4_PORT);
	nt::NetworkTableInstance client = nt::NetworkTableInstance::Create();
	client.SetServer("127.0.0.1", NT4_PORT);
	client.StartClient4("intrinsics-reader");
	auto root = client.GetTable("lumenvision");

	auto logger = std::make_shared<Logger>("LumenCoreTests-nt4-object-intrinsics.log");
	const nlohmann::json fuel = nlohmann::json::array({ { {"classId", 0}, {"className", "fuel"}, {"confidence", 0.9},
		{"box", {100, 120, 40, 40}}, {"frameWidth", 640}, {"frameHeight", 480}, {"yawDeg", 12.5}, {"pitchDeg", -3.0} } });
	CameraCalibrationResult calibration(600.0, 610.0, 320.0, 240.0, 0.3, { 0.1, -0.2, 0.0, 0.0, 0.05 }, 640, 480);
	auto calibrated = std::make_shared<FakeObjectDetector>(logger, "fuel-detector", fuel, calibration);
	// no detections at all: the result is an empty array, which the sink cannot tell from zero tags
	auto calibratedIdle = std::make_shared<FakeObjectDetector>(logger, "idle-detector", nlohmann::json::array(), calibration);
	auto uncalibrated = std::make_shared<FakeObjectDetector>(logger, "plain-detector", fuel, std::nullopt);

	NetworkTablesConfig config;
	config.serverAddress = "127.0.0.1";
	config.port = NT4_PORT;
	config.rootTable = "lumenvision";
	config.clientIdentity = "intrinsics-sink";
	auto ntSink = std::make_shared<NetworkTablesSink>(logger, "intrinsics-sink", config);
	REQUIRE(ntSink->BindSource(calibrated));
	REQUIRE(ntSink->BindSource(calibratedIdle));
	REQUIRE(ntSink->BindSource(uncalibrated));
	calibrated->Toggle(true);
	calibratedIdle->Toggle(true);
	uncalibrated->Toggle(true);
	static_cast<ISink&>(*ntSink).Toggle(true);

	const std::vector<double> expected{ 600.0, 0, 320.0, 0, 610.0, 240.0, 0, 0, 1 };
	for (const char* table : { "fuel-detector", "idle-detector" }) {
		INFO(table);
		auto node = root->GetSubTable(table);
		// two topics, each arriving on its own
		REQUIRE(WaitUntil([&] { return node->GetNumberArray("cameraIntrinsics", {}) == expected; }));
		REQUIRE(WaitUntil([&] { return node->GetNumberArray("cameraDistortion", {}) == calibration.distCoeffs; }));
	}
	// the detections themselves still arrive, and a detector without a calibration publishes no intrinsics
	auto plain = root->GetSubTable("plain-detector");
	REQUIRE(WaitUntil([&] { return plain->GetBoolean("hasTargets", false); }));
	REQUIRE_FALSE(plain->GetEntry("cameraIntrinsics").Exists());

	static_cast<ISink&>(*ntSink).Toggle(false);
	calibrated->Toggle(false);
	calibratedIdle->Toggle(false);
	uncalibrated->Toggle(false);
	client.StopClient();
	server.StopServer();
	nt::NetworkTableInstance::Destroy(client);
	nt::NetworkTableInstance::Destroy(server);
}

TEST_CASE("an object detector reports the calibration it was given, and none after an empty one", "[nt4]") {
	auto logger = std::make_shared<Logger>("LumenCoreTests-object-calibration.log");
	ObjectDetectionSink detector(logger, "calibration-detector", nullptr);
	REQUIRE_FALSE(detector.GetCalibration().has_value());

	detector.SetCalibration(CameraCalibrationResult(600.0, 610.0, 320.0, 240.0, 0.3, { 0.1, -0.2 }, 640, 480));
	std::optional<CameraCalibrationResult> reported = detector.GetCalibration();
	REQUIRE(reported.has_value());
	REQUIRE(reported->fx == 600.0);
	REQUIRE(reported->cy == 240.0);
	REQUIRE(reported->distCoeffs == std::vector<double>{ 0.1, -0.2 });

	// the server sends an empty calibration when the camera has none at its resolution
	detector.SetCalibration(CameraCalibrationResult());
	REQUIRE_FALSE(detector.GetCalibration().has_value());
}
