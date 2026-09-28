#include <catch2/catch_test_macros.hpp>
#include "ApriltagDetector.h"
#include "CameraCalibrationResult.h"
#include "NetworkTablesSink.h"
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
	REQUIRE(packet.size() >= 3); // schemaVersion (u16) + targetCount (u8) at minimum
	uint16_t schemaVersion = (static_cast<uint16_t>(packet[0]) << 8) | packet[1];
	REQUIRE(schemaVersion == 1);
	uint8_t targetCount = packet[2];
	REQUIRE(targetCount == ids.size());

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
