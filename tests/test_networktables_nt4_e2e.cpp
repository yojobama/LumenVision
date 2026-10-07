#include <catch2/catch_test_macros.hpp>
#include "ApriltagDetector.h"
#include "CameraCalibrationResult.h"
#include "NetworkTablesSink.h"
#include "ResultPacket.h"
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
	// NT4 has no cross-topic ordering: tags/ids may lag hasTargets, so wait for them.
	REQUIRE(WaitUntil([&] { return !detectorTable->GetNumberArray("tags/ids", std::vector<double>{}).empty(); }));
	// "result" is the last topic written per source, so wait for it too.
	REQUIRE(WaitUntil([&] { return detectorTable->GetRaw("result", std::vector<uint8_t>{}).size() >= 3; }));
	// First read of "latencyMs" needs a wait for the new subscription to sync.
	REQUIRE(WaitUntil([&] { return detectorTable->GetNumber("latencyMs", -1.0) >= 0.0; }));

	imageSource->Toggle(false);
	static_cast<ISink&>(*detector).Toggle(false);
	static_cast<ISink&>(*ntSink).Toggle(false);

	std::vector<double> ids = detectorTable->GetNumberArray("tags/ids", std::vector<double>{});
	REQUIRE_FALSE(ids.empty());
	REQUIRE(detectorTable->GetNumber("latencyMs", -1.0) >= 0.0);

	std::vector<uint8_t> packet = detectorTable->GetRaw("result", std::vector<uint8_t>{});
	PacketHeader packetHeader;
	std::vector<PacketTarget> packetTargets;
	REQUIRE(ParseResultPacket(packet, packetHeader, packetTargets)); // schema version 2
	REQUIRE(packetTargets.size() == ids.size());
	REQUIRE(packetTargets[0].fiducialId == static_cast<int16_t>(ids[0]));
	REQUIRE(packetHeader.sequenceId > 0);

	std::string status = clientTable->GetString(".status", "");
	nlohmann::json statusJson = nlohmann::json::parse(status, nullptr, false /* allow_exceptions */);
	REQUIRE_FALSE(statusJson.is_discarded());
	REQUIRE(statusJson.contains("uptimeSeconds"));

	client.StopClient();
	server.StopServer();
}

TEST_CASE("NetworkTablesSink surfaces robot-writable config/pipelineIndex and config/driverMode writes", "[nt4]") {
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
	robotSourceTable->PutBoolean("config/driverMode", true);
	robotSourceTable->PutNumber("config/pipelineIndex", 3);

	nlohmann::json requests;
	REQUIRE(WaitUntil([&] {
		requests = nlohmann::json::parse(ntSink->PollConfigRequests(), nullptr, false);
		return !requests.is_discarded() && !requests.empty();
	}));
	REQUIRE(requests.size() == 1);
	REQUIRE(requests[0]["sourceId"] == "some-detector-id");
	REQUIRE(requests[0]["driverMode"] == true);
	REQUIRE(requests[0]["pipelineIndex"] == 3);

	// Requests are consumed on poll.
	REQUIRE(ntSink->PollConfigRequests() == "[]");

	robot.StopClient();
	server.StopServer();
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
	// As published by photoncompat LumenCoprocessor.setRecording().
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
	front->PutNumber("config/inputSnapshot", 1);
	front->PutNumber("config/outputSnapshot", 1);
	robot.GetTable("lumenvision")->PutNumber("config/ledMode", 2);

	nlohmann::json requests;
	REQUIRE(WaitUntil([&] {
		requests = nlohmann::json::parse(ntSink->PollConfigRequests(), nullptr, false);
		return !requests.is_discarded() && !requests.empty() && requests[0].size() >= 4;
	}));
	// the robot addressed the camera by name; the request comes back keyed by the node id behind it
	REQUIRE(requests[0]["sourceId"] == "node-7");
	REQUIRE(requests[0]["fpsLimit"] == 15);
	REQUIRE(requests[0]["inputSnapshots"] == 1);
	REQUIRE(requests[0]["outputSnapshots"] == 1);

	int led = -2;
	REQUIRE(WaitUntil([&] { led = ntSink->PollLedRequest(); return led != -2; }));
	REQUIRE(led == 2);
	REQUIRE(ntSink->PollLedRequest() == -2);

	ntSink->PublishNodeStatus("node-7", 3, true, 15);
	ntSink->SetLedStatus(2);
	REQUIRE(WaitUntil([&] { return front->GetNumber("status/pipelineIndex", -1.0) == 3.0; }));
	// NT4 has no cross-topic ordering, so each read-back is awaited on its own
	REQUIRE(WaitUntil([&] { return front->GetBoolean("status/driverMode", false); }));
	REQUIRE(WaitUntil([&] { return front->GetNumber("status/fpsLimit", -1.0) == 15.0; }));
	REQUIRE(WaitUntil([&] { return robot.GetTable("lumenvision")->GetNumber("status/ledMode", -5.0) == 2.0; }));

	robot.StopClient();
	server.StopServer();
}

// A NetworkTablesSink the server rebuilds in place (the NT settings changed) must keep publishing its bound detector's results.
TEST_CASE("a NetworkTablesSink rebuilt under the same id keeps publishing the bound detector's results", "[nt4][e2e][rebuild]") {
	const std::string pgmPath = std::string(LUMEN_VKAPRILTAG_SAMPLE_DIR) + "/grayimage.pgm";
	cv::Mat gray = cv::imread(pgmPath, cv::IMREAD_GRAYSCALE);
	REQUIRE_FALSE(gray.empty());
	cv::Mat bgr;
	cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);

	constexpr unsigned int NT3_PORT = 17811;
	constexpr unsigned int NT4_PORT = 17812;
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
	REQUIRE(WaitUntil([&] { return detectorTable->GetNumberArray("tags/ids", {}).size() > 0; }, 200, 50));

	// rebuilt the way Manager::DeleteSink + CreateNetworkTablesSinkForServer(id, ...) do it: the old sink is toggled off and destroyed first
	static_cast<ISink&>(*ntSink).Toggle(false);
	ntSink.reset();
	config.clientIdentity = "rebuild-2";
	ntSink = std::make_shared<NetworkTablesSink>(logger, "rebuild-sink", config);
	REQUIRE(ntSink->BindSource(detector));
	static_cast<ISink&>(*ntSink).Toggle(true);

	// the old sink's values are gone with its client; only the rebuilt sink can publish them again
	REQUIRE(WaitUntil([&] { return clientTable->GetNumber("heartbeat", -1.0) >= 0.0; }, 200, 50));
	detectorTable->GetEntry("tags/ids").SetDoubleArray({});
	REQUIRE(WaitUntil([&] { return detectorTable->GetNumberArray("tags/ids", {}).size() > 0; }, 200, 50));

	static_cast<ISink&>(*ntSink).Toggle(false);
	static_cast<ISink&>(*detector).Toggle(false);
	imageSource->Toggle(false);
	client.StopClient();
	server.StopServer();
}
