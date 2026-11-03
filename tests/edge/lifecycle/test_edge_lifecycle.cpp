// Edge-case hunt, area D (graph lifecycle and concurrency): ISink/ISource attach/detach, destruction while frames flow, and Manager's graph
// edits racing its readers. Every case asserts the correct behaviour; see each one for which run (Release, ASan, TSan) shows it failing.
#include <catch2/catch_test_macros.hpp>
#include "ISource.h"
#include "ISink.h"
#include "Manager.h"
#include "StageProfile.h"
#include "Frame.h"
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// A source that publishes on its own capture thread as fast as it can (periodUs = 0) or at a fixed period, like a camera.
class EdgeBusySource : public ISource {
public:
	EdgeBusySource(std::string id, int periodUs) : ISource(nullptr, std::move(id)), m_PeriodUs(periodUs) {}
	~EdgeBusySource() override { Toggle(false); }

	// lets a test publish from its own thread, to time SetLatestResult
	void Publish() { SetLatestResult(SourceResult(nlohmann::json::object(), cv::Mat(1, 1, CV_8UC1))); }

protected:
	void CaptureFrame() override
	{
		SetLatestResult(SourceResult(nlohmann::json::object(), cv::Mat(1, 1, CV_8UC1)));
		if (m_PeriodUs > 0) std::this_thread::sleep_for(std::chrono::microseconds(m_PeriodUs));
		else std::this_thread::yield();
	}

private:
	int m_PeriodUs;
};

// A source with no thread that never publishes (a camera that has not produced a frame yet).
class EdgeIdleSource : public ISource {
public:
	explicit EdgeIdleSource(std::string id) : ISource(nullptr, std::move(id)) { m_DoNotLoadCaptureThread = true; }
};

// A terminal sink taking up to `maxSources` sources (NetworkTablesSink takes 16), counting what it is given and how often it wakes.
class EdgeCountingSink : public ISink {
public:
	EdgeCountingSink(std::string id, int maxSources) : ISink(nullptr, maxSources, false, false, std::move(id)) {}
	~EdgeCountingSink() override { Toggle(false); }
	std::atomic<long long> processed{ 0 };
	std::atomic<long long> polls{ 0 };

protected:
	void Process(const std::vector<SourceResult>& results) override { processed += static_cast<long long>(results.size()); }
	void OnPoll() override { polls++; }
};

std::string EdgeId(const char* prefix, int i) { return std::string(prefix) + std::to_string(i); }

} // namespace

// D1. ISink::m_Sources is a plain vector that BindSource/UnbindSource change on the caller's thread while ProcessingThreadLoop iterates it on
// the sink's own thread. The server does exactly this: NetworkTablesControlService binds every new detector onto the running NT sink each tick
// (EnsureSourceBoundToSink), and DeleteSource/DeleteSink unbind a node from every running sink. Sources are kept alive by the test and only
// the last element is ever erased, so a Release run does not crash; ThreadSanitizer reports the race on the vector.
TEST_CASE("D: binding and unbinding a source on a running sink does not race its processing thread", "[edge][lifecycle]") {
	auto camera = std::make_shared<EdgeBusySource>("d1-camera", 200);
	auto sink = std::make_shared<EdgeCountingSink>("d1-nt", 16);
	REQUIRE(sink->BindSource(camera));
	std::vector<std::shared_ptr<EdgeIdleSource>> detectors;
	for (int i = 0; i < 8; i++) detectors.push_back(std::make_shared<EdgeIdleSource>(EdgeId("d1-det", i)));
	// grow the vector once before the thread starts, so a later push_back never reallocates under it in this run
	for (auto& detector : detectors) REQUIRE(sink->BindSource(detector));
	for (auto it = detectors.rbegin(); it != detectors.rend(); ++it) REQUIRE(sink->UnbindSource((*it)->GetID()));

	camera->Toggle(true);
	sink->Toggle(true);
	const auto deadline = Clock::now() + std::chrono::milliseconds(800);
	int cycles = 0;
	while (Clock::now() < deadline) {
		auto& detector = detectors[cycles % detectors.size()];
		REQUIRE(sink->BindSource(detector));     // a new detector appears: the NT service binds it to the running NT sink
		std::this_thread::sleep_for(std::chrono::microseconds(300));
		REQUIRE(sink->UnbindSource(detector->GetID())); // the detector is deleted: DeleteSink unbinds it from every running sink
		cycles++;
	}
	sink->Toggle(false);
	camera->Toggle(false);
	REQUIRE(sink->processed.load() > 0);
	REQUIRE(cycles > 10);
}

// D1 (crash form). The same race when the unbind drops the last reference to the source, as Manager::DeleteSource does once it erases the
// source from its own map: the processing thread is still dereferencing the vector slot it cached (range-for caches end()). ASan reports a
// heap-use-after-free in ISink::ProcessingThreadLoop. Hidden: it can crash a Release run.
TEST_CASE("D: unbinding the last reference to a source while the sink processes does not touch the freed source", "[edge][lifecycle][crash]") {
	auto camera = std::make_shared<EdgeBusySource>("d1c-camera", 0);
	auto sink = std::make_shared<EdgeCountingSink>("d1c-sink", 4);
	REQUIRE(sink->BindSource(camera));
	camera->Toggle(true);
	sink->Toggle(true);
	const auto deadline = Clock::now() + std::chrono::seconds(4);
	int cycles = 0;
	while (Clock::now() < deadline) {
		{
			auto doomed = std::make_shared<EdgeIdleSource>(EdgeId("d1c-src", cycles));
			REQUIRE(sink->BindSource(doomed));
		} // the sink now holds the only reference, as after Manager::DeleteSource erases its own
		REQUIRE(sink->UnbindSource(EdgeId("d1c-src", cycles)));
		cycles++;
	}
	sink->Toggle(false);
	camera->Toggle(false);
	REQUIRE(cycles > 0);
}

// D2. The listener ISink::BindSource registers on a source checks the sink's alive flag and then calls NotifyDataAvailable() on the raw `this`;
// nothing stops the sink being destroyed between the check and the call (the weak_ptr keeps only the flag alive). Manager::DeleteSink destroys a
// sink while its upstream camera keeps publishing (it never unbinds the deleted sink from its own sources), so the camera's thread can lock and
// write a freed sink's m_WakeMutex/m_DataAvailable/m_WakeCV. ASan: heap-use-after-free in ISink::NotifyDataAvailable; TSan: a race between the
// listener and operator delete. Hidden: it can crash or hang a Release run (locking freed memory).
TEST_CASE("D: a sink destroyed while its source keeps publishing is never notified after it is freed", "[edge][lifecycle][crash]") {
	auto camera = std::make_shared<EdgeBusySource>("d2-camera", 0);
	camera->Toggle(true);
	const auto deadline = Clock::now() + std::chrono::seconds(4);
	int created = 0;
	while (Clock::now() < deadline) {
		std::vector<std::shared_ptr<EdgeCountingSink>> previews;
		for (int i = 0; i < 16; i++) {
			previews.push_back(std::make_shared<EdgeCountingSink>(EdgeId("d2-sink", created++), 1));
			REQUIRE(previews.back()->BindSource(camera));
		}
		std::this_thread::sleep_for(std::chrono::microseconds(200));
		// destroyed one by one with the camera still running, as Manager::DeleteSink does
		while (!previews.empty()) previews.pop_back();
		// keep the dead-listener list (finding D3) from making the camera too slow to hit the window
		if (created > 4000) break;
	}
	camera->Toggle(false);
	REQUIRE(created > 0);
}

// D3. UnbindSource removes the frame consumer but not the result listener, so an unbound sink is still woken by every frame of the source it
// left (and a sink rebound N times is woken N times per frame). The camera publishes every millisecond; once unbound, the sink should only see
// its own 100 ms safety poll (about 10 wake-ups a second).
TEST_CASE("D: a sink unbound from a source is no longer woken by that source's frames", "[edge][lifecycle]") {
	auto camera = std::make_shared<EdgeBusySource>("d3-camera", 1000);
	auto sink = std::make_shared<EdgeCountingSink>("d3-sink", 1);
	REQUIRE(sink->BindSource(camera));
	camera->Toggle(true);
	sink->Toggle(true);
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	REQUIRE(sink->UnbindSource("d3-camera"));
	REQUIRE_FALSE(sink->IsBoundTo("d3-camera"));

	const long long pollsBefore = sink->polls.load();
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	const long long wakeUps = sink->polls.load() - pollsBefore;
	sink->Toggle(false);
	camera->Toggle(false);
	INFO("wake-ups in 1 s after the unbind: " << wakeUps);
	CHECK(wakeUps < 40);
}

// D3 (growth). A destroyed sink's listener (and frame-consumer entry) stays on its source forever and is copied and called on every frame,
// so a long-lived camera gets slower to publish with every sink that was ever bound to it and deleted (graph edits, profile switches,
// calibration previews). After 20000 such sinks one SetLatestResult should cost about what it did on a fresh source.
TEST_CASE("D: sinks bound to a source and then destroyed do not make its every frame slower", "[edge][lifecycle]") {
	auto camera = std::make_shared<EdgeBusySource>("d3g-camera", 0); // never toggled on: the test publishes
	auto timePublishes = [&](int count) {
		const auto start = Clock::now();
		for (int i = 0; i < count; i++) camera->Publish();
		return std::chrono::duration<double, std::micro>(Clock::now() - start).count() / count;
	};
	{
		auto live = std::make_shared<EdgeCountingSink>("d3g-live", 1); // one real consumer, as in normal use
		REQUIRE(live->BindSource(camera));
		timePublishes(200); // warm-up
		const double freshUs = timePublishes(500);

		for (int i = 0; i < 20000; i++) {
			auto preview = std::make_shared<EdgeCountingSink>(EdgeId("d3g-dead", i), 1);
			REQUIRE(preview->BindSource(camera));
		}
		const double afterUs = timePublishes(200);
		INFO("one publish: " << freshUs << " us on a fresh source, " << afterUs << " us after 20000 deleted sinks");
		CHECK(afterUs < freshUs * 5 + 20);
	}
}

// D4. Manager has no lock, and the server calls it from several threads at once: StateChannel's broadcast loop reads GetFrameCount/
// GetLatencyUs/IsSinkActive for every node, NetworkTablesControlService's tick calls EnsureSourceBoundToSink, and the REST handlers create and
// delete nodes (std::map insert/erase) at the same time. TSan reports the races on Manager::m_Sources/m_Sinks; ASan can report a
// heap-use-after-free on a map node. Hidden: concurrent map erase can crash a Release run.
TEST_CASE("D: Manager's readers on one thread and graph edits on another do not race", "[edge][lifecycle][crash]") {
	Manager manager;
	const std::string image = std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg";
	const int camera = manager.CreateImageFileSource(image, 9100);
	const int preview = manager.CreateMjpegSink(9101, 80);
	REQUIRE(manager.BindSourceToSink(camera, preview));

	std::atomic<bool> stop{ false };
	std::atomic<long long> reads{ 0 };
	// the state broadcast (StateChannel.BuildSnapshot) and the NT control tick
	std::thread broadcaster([&] {
		while (!stop) {
			for (int id = 9100; id < 9140; id++) {
				(void)manager.GetFrameCount(id);
				(void)manager.GetLatencyUs(id);
				(void)manager.IsSinkActive(id);
				(void)manager.EnsureSourceBoundToSink(id, preview);
			}
			reads++;
		}
	});
	// the REST handlers: cameras and previews added and deleted while the broadcast runs
	const auto deadline = Clock::now() + std::chrono::seconds(3);
	int edits = 0;
	while (Clock::now() < deadline) {
		const int id = 9110 + (edits % 20);
		if (edits % 2 == 0) manager.CreateImageFileSource(image, id);
		else manager.CreateMjpegSink(id, 80);
		if (edits % 2 == 0) manager.DeleteSource(id);
		else manager.DeleteSink(id);
		edits++;
	}
	stop = true;
	broadcaster.join();
	REQUIRE(reads.load() > 0);
	REQUIRE(edits > 0);
}

// D5. Manager::CreateApriltagDetector(calibration, tagSize) - the overload SinkManager.AddApriltagSinkForCamera (POST
// /apriltagSink/createFromCamera) uses - registers the detector in m_Sinks only, unlike every other detector overload which registers it in both
// maps. Its output can then never be bound: NetworkTablesControlService's EnsureSourceBoundToSink(detector, ntSink) fails every tick, so its
// tags never reach the robot, GetFrameCount reports 0 fps, and no preview can show it (until a restart recreates it through CreateApriltagDetector(id)).
TEST_CASE("D: an AprilTag detector created with a camera's calibration can feed downstream sinks", "[edge][lifecycle]") {
	Manager manager;
	CameraCalibrationResult calibration;
	calibration.fx = calibration.fy = 600.0;
	calibration.cx = 320.0;
	calibration.cy = 240.0;
	calibration.imageWidth = 640;
	calibration.imageHeight = 480;
	calibration.distCoeffs = { 0, 0, 0, 0, 0 };
	const int detector = manager.CreateApriltagDetector(calibration, 0.1651);
	const int nt = manager.CreateMjpegSink(80); // stands in for the NT sink: any sink that binds the detector's output

	CHECK(manager.EnsureSourceBoundToSink(detector, nt));
	CHECK(manager.GetAllSources().size() == 1); // the detector's source half, like CreateApriltagDetector(id)
	manager.Shutdown();
}

// D6. Nothing stops a dual-role node (detector, crop) being bound to itself, or two of them into a loop, and the REST bind (PATCH /sink/bind)
// does not either; the graph page only blocks the self-edge, not A->B->A (dragging A's input onto B's output). A loop runs on its own, with
// no camera in it: each node's result wakes the other, at full CPU, forever. A self-bound node also holds a shared_ptr to itself, so DeleteSink
// (which unbinds only the OTHER sinks from it) leaks it with its detector backend.
TEST_CASE("D: a dual-role node cannot be bound to itself", "[edge][lifecycle]") {
	Manager manager;
	const int detector = manager.CreateApriltagDetector();
	const bool bound = manager.BindSourceToSink(detector, detector);
	CHECK_FALSE(bound);
	manager.DeleteSink(detector);
	// a node that is gone has no profile left in the registry (it holds a weak_ptr; the node holds the profile)
	const std::string profiles = manager.GetNodeProfilesJson();
	INFO("profiles after DeleteSink: " << profiles);
	CHECK(profiles.find("\"" + std::to_string(detector) + "\"") == std::string::npos);
	manager.Shutdown();
}

TEST_CASE("D: two detectors rewired into a loop do not keep processing with no camera feeding them", "[edge][lifecycle]") {
	Manager manager;
	const int camera = manager.CreateImageFileSource(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
	const int a = manager.CreateApriltagDetector();
	const int b = manager.CreateApriltagDetector();
	const int preview = manager.CreateMjpegSink(80); // a watched preview on B, so B publishes annotated frames
	REQUIRE(manager.BindSourceToSink(camera, a));
	REQUIRE(manager.BindSourceToSink(a, b));
	REQUIRE(manager.BindSourceToSink(b, preview));
	manager.StartSinkById(a);
	manager.StartSinkById(b);
	manager.StartSinkById(preview);
	const auto deadline = Clock::now() + std::chrono::seconds(5);
	while (manager.GetFrameCount(b) < 1 && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
	REQUIRE(manager.GetFrameCount(b) >= 1);

	// the graph page's drag of A's input edge from the camera onto B's output: unbind, then bind
	REQUIRE(manager.UnbindSourceFromSink(a));
	const bool bound = manager.BindSourceToSink(b, a);
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	const uint64_t before = manager.GetFrameCount(a);
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	const uint64_t frames = manager.GetFrameCount(a) - before;
	INFO("bound into a loop: " << bound << "; A published " << frames << " results in 1 s with no camera feeding the loop");
	CHECK((!bound || frames < 5));
	manager.Shutdown();
}

// D7. Manager::UnbindSourceFromSink(sinkId) takes no source id and unbinds only the FIRST of the sink's sources it meets in m_Sources (the
// lowest id), then returns. A stereo node has two: the graph page's "disconnect" on a stereo edge (PATCH /sink/unbind, "Disconnected both
// cameras") leaves one camera bound, and binding the node to a new pair (POST stereo bind -> BindStereoSources) then fails because the node
// is still holding a camera (2 sources max): the second camera is refused and the node is left fed by one old and one new camera.
TEST_CASE("D: disconnecting a stereo node's cameras frees it to be bound to another pair", "[edge][lifecycle]") {
	Manager manager;
	const std::string image = std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg";
	const int leftA = manager.CreateImageFileSource(image, 9201);
	const int rightA = manager.CreateImageFileSource(image, 9202);
	const int leftB = manager.CreateImageFileSource(image, 9203);
	const int rightB = manager.CreateImageFileSource(image, 9204);
	const int stereo = manager.CreateStereoCalibrator(9210);
	REQUIRE(manager.BindStereoSources(stereo, leftA, rightA));

	REQUIRE(manager.UnbindAllSources(stereo)); // the UI's disconnect on the stereo edge (the Server calls this one)
	const bool rebound = manager.BindStereoSources(stereo, leftB, rightB);
	CHECK(rebound);
	// and nothing of the old pair is still feeding the node: unbinding it again finds nothing left to remove
	const bool stillHadOldCamera = manager.UnbindAllSources(stereo) && !rebound;
	CHECK_FALSE(stillHadOldCamera);
	manager.Shutdown();
}

// D8. ISink::BindSource does not check whether the source is already bound, and Manager::BindSourceToSink (PATCH /sink/bind) does not either
// (only EnsureSourceBoundToSink does). NetworkTablesControlService has already bound every detector to the NT sink; connecting the same
// detector to the NT sink on the graph page binds it a second time, and from then on every result of that detector is handed to the sink
// twice per frame (published to the robot twice) and takes two of the NT sink's 16 slots. One unbind then removes only one of the copies.
TEST_CASE("D: binding a source that is already bound does not deliver its results twice", "[edge][lifecycle]") {
	auto detector = std::make_shared<EdgeBusySource>("d8-detector", 0); // never toggled on: the test publishes
	auto nt = std::make_shared<EdgeCountingSink>("d8-nt", 16);
	REQUIRE(nt->BindSource(detector));       // NetworkTablesControlService's EnsureSourceBoundToSink
	(void)nt->BindSource(detector);          // the user connects the same detector to the NT sink in the graph
	nt->Toggle(true);
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	const long long before = nt->processed.load();
	for (int i = 0; i < 20; i++) {
		detector->Publish();
		std::this_thread::sleep_for(std::chrono::milliseconds(15)); // well over one wake-up per frame
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	const long long delivered = nt->processed.load() - before;
	nt->Toggle(false);
	INFO("results handed to the sink for 20 frames of one detector: " << delivered);
	CHECK(delivered <= 20);
	// and one unbind (the graph page's disconnect) leaves it unbound
	REQUIRE(nt->UnbindSource("d8-detector"));
	CHECK_FALSE(nt->IsBoundTo("d8-detector"));
}

namespace {
// Calls node->Toggle(true) from two threads released together, up to `attempts` times on fresh nodes; returns the attempt that did not
// finish within a second (the node and its threads are then leaked on purpose: they can never be joined), or -1.
template <typename Node, typename Make>
int EdgeConcurrentEnableHangsAt(Make make, int attempts)
{
	for (int attempt = 0; attempt < attempts; attempt++) {
		std::shared_ptr<Node> node = make(attempt);
		auto go = std::make_shared<std::atomic<bool>>(false);
		auto done = std::make_shared<std::atomic<int>>(0);
		std::thread robot([node, go, done] { while (!*go) {} node->Toggle(true); (*done)++; });  // NetworkTablesControlService: <node>/config/enabled
		std::thread web([node, go, done] { while (!*go) {} node->Toggle(true); (*done)++; });    // PATCH /sink/enable from the web UI
		*go = true;
		const auto deadline = Clock::now() + std::chrono::seconds(1);
		while (*done < 2 && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (*done < 2) {
			robot.detach();
			web.detach();
			new std::shared_ptr<Node>(node); // never destroyed: its destructor would join a thread nothing will stop
			return attempt;
		}
		robot.join();
		web.join();
		node->Toggle(false);
	}
	return -1;
}
} // namespace

// D10. ISink::Toggle and ISource::Toggle are check-then-act on a plain m_ToggleState (`if (toggle == m_ToggleState) return;` then start the
// thread, then set the flag), and the server calls them from several threads: SinkManager.SetSinkEnabled runs on the NT control service's
// thread for a robot's <node>/config/enabled and on a REST thread for the UI's enable switch, and both enable the node's camera
// (SourceManager.EnableSourceById -> ISource::Toggle(true)), so two nodes on one camera enabled together race on the camera. When both
// callers pass the check, the second `m_Thread = std::jthread(...)` move-assigns over a running jthread, which request_stop()s and joins
// it; the loop only watches m_ShouldTerminate, so that join never returns: the second caller hangs and a stray thread keeps running.
// Making m_ToggleState atomic (the D0 fix) does not close this window. Hidden: a hit leaves threads that can never be joined.
TEST_CASE("D: enabling a sink from two threads at once returns and leaves one processing thread", "[edge][lifecycle][hang]") {
	const int hungAt = EdgeConcurrentEnableHangsAt<EdgeCountingSink>(
		[](int i) { return std::make_shared<EdgeCountingSink>(EdgeId("d10-sink", i), 1); }, 2000);
	INFO("two concurrent Toggle(true) calls on one sink did not return (attempt " << hungAt << ")");
	CHECK(hungAt == -1);
}

TEST_CASE("D: enabling a camera from two threads at once returns and leaves one capture thread", "[edge][lifecycle][hang]") {
	const int hungAt = EdgeConcurrentEnableHangsAt<EdgeBusySource>(
		[](int i) { return std::make_shared<EdgeBusySource>(EdgeId("d10-camera", i), 1000); }, 2000);
	INFO("two concurrent Toggle(true) calls on one camera did not return (attempt " << hungAt << ")");
	CHECK(hungAt == -1);
}
