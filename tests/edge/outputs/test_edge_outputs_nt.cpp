// Edge-case hunt, area E (outputs, encoding side): NetworkTablesSink.
#ifdef LUMEN_WITH_NT4
#include <catch2/catch_test_macros.hpp>
#include "NetworkTablesSink.h"
#include "ResultPacket.h"
#include "edge_outputs_access.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <map>
#include <thread>

struct EdgeOutputsNtProcessTag {
	using type = void (NetworkTablesSink::*)(const std::vector<SourceResult>&);
	friend type get(EdgeOutputsNtProcessTag);
};
template struct EdgeOutputsRob<EdgeOutputsNtProcessTag, &NetworkTablesSink::Process>;
namespace {

template <typename Predicate>
bool EdgeOutputsWaitUntil(Predicate predicate, int maxAttempts = 100, int delayMs = 50)
{
	for (int attempt = 0; attempt < maxAttempts; attempt++) {
		if (predicate()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
	}
	return predicate();
}
}

// Server/NetworkTablesControlService.RefreshAliases calls SetNodeAlias once per node whose table name changed. When two cameras swap names
// (or one takes the name another just gave up) the second call erases the table->node mapping the first call has just created, because
// SetNodeAlias erases m_AliasToNode[its own old alias] without checking that the entry still points at itself. From then on a robot's
// config write on that camera's table (enabled / fpsLimit / snapshot / recording / robotToCamera) is reported under the table name instead
// of the node id, which the server cannot parse as a node id, so the write is silently dropped.
TEST_CASE("E: after two cameras swap names, a robot config write on either name still reaches its node id", "[edge][outputs]") {
	constexpr unsigned int NT3_PORT = 18541;
	constexpr unsigned int NT4_PORT = 18542;
	nt::NetworkTableInstance server = nt::NetworkTableInstance::Create();
	server.StartServer("", "127.0.0.1", NT3_PORT, NT4_PORT);

	auto logger = std::make_shared<Logger>("LumenCoreTests-edge-outputs-alias.log");
	NetworkTablesConfig config;
	config.serverAddress = "127.0.0.1";
	config.port = NT4_PORT;
	config.clientIdentity = "edge-outputs-alias-sink";
	auto ntSink = std::make_shared<NetworkTablesSink>(logger, "edge-outputs-alias-sink", config);

	// node 1 is "front", node 2 is "back"; then the user swaps the two names and the server re-applies both aliases
	ntSink->SetNodeAlias("1", "front");
	ntSink->SetNodeAlias("2", "back");
	ntSink->SetNodeAlias("1", "back");
	ntSink->SetNodeAlias("2", "front");

	nt::NetworkTableInstance robot = nt::NetworkTableInstance::Create();
	robot.SetServer("127.0.0.1", NT4_PORT);
	robot.StartClient4("edge-outputs-alias-robot");
	robot.GetTable("lumenvision/back")->PutBoolean("config/enabled", false);   // camera 1, by its new name
	robot.GetTable("lumenvision/front")->PutNumber("config/fpsLimit", 7);       // camera 2, by its new name

	nlohmann::json requests = nlohmann::json::array();
	REQUIRE(EdgeOutputsWaitUntil([&] {
		nlohmann::json polled = nlohmann::json::parse(ntSink->PollConfigRequests(), nullptr, false);
		if (!polled.is_discarded()) for (const auto& r : polled) requests.push_back(r);
		return requests.size() >= 2;
	}));

	std::map<std::string, nlohmann::json> bySource;
	for (const auto& r : requests) bySource[r["sourceId"].get<std::string>()] = r;
	UNSCOPED_INFO("config requests: " << requests.dump());
	CHECK(bySource.count("1") == 1);
	CHECK(bySource.count("2") == 1);
	if (bySource.count("1")) CHECK(bySource["1"]["enabled"] == false);
	if (bySource.count("2")) CHECK(bySource["2"]["fpsLimit"] == 7);

	ntSink.reset();
	robot.StopClient();
	server.StopServer();
	nt::NetworkTableInstance::Destroy(robot);
	nt::NetworkTableInstance::Destroy(server);
}

// ObjectDetectionSink puts each detection's className (a line of the model's labels file, read as raw bytes) into its JSON. The NT sink
// re-serialises an object detector's result for the "raw" topic with nlohmann::json::dump(), whose default error handler throws
// type_error.316 on a string that is not valid UTF-8, e.g. a labels file saved in Windows-1252 ("caf\xE9"). The throw escapes Process():
// on the sink's thread that is the known X1 terminate; with X1 fixed, every frame of that detector would still be dropped (no result packet,
// no hasTargets) because the dump happens before the packet is built. Called directly here so the failure is an assertion, not a crash.
TEST_CASE("E: an object detection whose class name is not valid UTF-8 is still published", "[edge][outputs]") {
	auto logger = std::make_shared<Logger>("LumenCoreTests-edge-outputs-utf8.log");
	NetworkTablesConfig config;
	config.serverAddress = "127.0.0.1";
	config.port = 18544; // nothing listens there: publishing needs no server
	config.clientIdentity = "edge-outputs-utf8-sink";
	auto ntSink = std::make_shared<NetworkTablesSink>(logger, "edge-outputs-utf8-sink", config);

	const std::string latin1Name = std::string("caf") + static_cast<char>(0xE9); // "café" in Windows-1252
	nlohmann::json detections = nlohmann::json::array({ { {"classId", 3}, {"className", latin1Name}, {"confidence", 0.8},
		{"box", {10, 20, 30, 40}}, {"frameWidth", 640}, {"frameHeight", 480} } });
	SourceResult result(detections, std::nullopt, SourceResult::NowUs());
	result.sourceId = "object-detector";
	result.frameNumber = 1;

	auto process = get(EdgeOutputsNtProcessTag{});
	CHECK_NOTHROW((ntSink.get()->*process)(std::vector<SourceResult>{ result }));
}

// A hidden twin of the case above, through the sink's own processing thread as in production: shows the process terminating (X1's
// consequence, reached through this new trigger). Run by exact name only.
namespace {
class EdgeOutputsFakeObjectDetector : public ISource {
public:
	EdgeOutputsFakeObjectDetector(std::shared_ptr<Logger> logger, std::string id, nlohmann::json detections)
		: ISource(logger, id), m_Detections(std::move(detections)) {}
protected:
	void CaptureFrame() override {
		SetLatestResult(SourceResult(m_Detections, std::nullopt));
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
private:
	nlohmann::json m_Detections;
};
}

TEST_CASE("E: (crash) a non-UTF-8 class name on a running NetworkTablesSink", "[edge][outputs]") {
	auto logger = std::make_shared<Logger>("LumenCoreTests-edge-outputs-utf8-thread.log");
	NetworkTablesConfig config;
	config.serverAddress = "127.0.0.1";
	config.port = 18546;
	config.clientIdentity = "edge-outputs-utf8-thread-sink";
	auto ntSink = std::make_shared<NetworkTablesSink>(logger, "edge-outputs-utf8-thread-sink", config);
	const std::string latin1Name = std::string("caf") + static_cast<char>(0xE9);
	auto detector = std::make_shared<EdgeOutputsFakeObjectDetector>(logger, "object-detector", nlohmann::json::array({ { {"classId", 3},
		{"className", latin1Name}, {"confidence", 0.8}, {"box", {10, 20, 30, 40}}, {"frameWidth", 640}, {"frameHeight", 480} } }));
	REQUIRE(ntSink->BindSource(detector));
	detector->Toggle(true);
	static_cast<ISink&>(*ntSink).Toggle(true);
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	static_cast<ISink&>(*ntSink).Toggle(false);
	detector->Toggle(false);
	SUCCEED("the process survived");
}
#endif // LUMEN_WITH_NT4
