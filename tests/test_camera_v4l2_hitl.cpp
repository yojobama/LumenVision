#include <catch2/catch_test_macros.hpp>
#include "Manager.h"
#include "CameraSource.h"
#include <chrono>
#include <thread>

// Hardware-in-the-loop camera tests; self-skips when no camera is attached.
TEST_CASE("V4L2 camera backend enumerates real modes and honours an explicit SetMode request", "[hitl][camera]") {
    Manager manager;
    auto cameras = manager.EnumerateAvailableCameras();
    if (cameras.empty()) {
        SKIP("no camera hardware available on this machine");
    }

    int sourceId = manager.CreateCameraSource(cameras[0]);

    auto modes = manager.GetCameraModes(sourceId);
    REQUIRE_FALSE(modes.empty());

    REQUIRE(manager.SetCameraMode(sourceId, modes[0]));

    CameraMode current = manager.GetCameraCurrentMode(sourceId);
    REQUIRE(current.width == modes[0].width);
    REQUIRE(current.height == modes[0].height);
}

// Starts the capture thread and confirms frames arrive through the V4L2 mmap/poll/DQBUF path.
TEST_CASE("V4L2 camera backend actually captures frames once started", "[hitl][camera]") {
    Manager manager;
    auto cameras = manager.EnumerateAvailableCameras();
    if (cameras.empty()) {
        SKIP("no camera hardware available on this machine");
    }

    auto logger = std::make_shared<Logger>("LumenCoreTests-hitl.log");
    CameraFrameSource source(cameras[0].path, cameras[0].name, logger, "hitl-capture-test");

    source.Toggle(true);
	// Allow a few frame intervals, even at the slowest advertised mode (5fps).
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    source.Toggle(false);

    REQUIRE(source.GetCurrentFrameCount() > 0);

    SourceResult result = source.GetLatestResult();
    REQUIRE(result.frame.has_value());
    REQUIRE_FALSE(result.frame->empty());
}
