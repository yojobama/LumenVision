// Edge-case hunt, real-camera checks on the Orange Pi (skipped where no camera is attached): A1 (a crop of a live frame) and A5 (a camera that
// keeps failing to deliver). They use whatever cameras the board has, through the real V4L2 backend.
#ifdef __linux__
#include <catch2/catch_test_macros.hpp>
#include "CameraSource.h"
#include "MjpegHwEncoder.h"
#include "RecordSink.h"
#include "RoiSource.h"
#include "Frame.h"
#include "Logger.h"

#include <dirent.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <atomic>
#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "V4l2CameraBackend.h"

namespace {

struct EdgeCamera {
	std::string path;
	std::string name;
};

// every /dev/video node that opens as a capture device, once per physical camera (the first node of each)
std::vector<EdgeCamera> EdgeCameras()
{
	std::vector<EdgeCamera> cameras;
	std::vector<std::string> seenNames;
	for (int i = 0; i < 16; i++) {
		const std::string path = "/dev/video" + std::to_string(i);
		V4l2CameraBackend probe;
		if (!probe.Open(path)) continue;
		std::ifstream nameFile("/sys/class/video4linux/video" + std::to_string(i) + "/name");
		std::string name;
		std::getline(nameFile, name);
		bool known = false;
		for (const std::string& seen : seenNames) known |= seen == name;
		if (known) continue;
		seenNames.push_back(name);
		cameras.push_back({ path, name.empty() ? path : name });
	}
	return cameras;
}

int EdgeCountLines(const std::filesystem::path& file, const std::string& needle)
{
	std::ifstream in(file);
	std::string line;
	int count = 0;
	while (std::getline(in, line))
		if (line.find(needle) != std::string::npos) count++;
	return count;
}

}

// A1 on hardware. Frame::Roi of a BGR frame dropped the FramePool owner, so the buffer of a crop went back to the pool when the camera's frame was
// released and the next capture wrote over it: a stereo pair split from one side-by-side camera (RoiSource) kept a "left eye" that kept changing.
// A raw mode (YUYV) is decoded into a pooled BGR buffer, which is the path with the bug (a hardware-decoded MJPEG is NV12 and was never affected).
TEST_CASE("A1: a crop of a live camera frame keeps its pixels while the camera goes on capturing", "[edge][board-media][v4l2][camera]")
{
	const std::vector<EdgeCamera> cameras = EdgeCameras();
	if (cameras.empty()) SKIP("no camera attached");

	bool ran = false;
	for (const EdgeCamera& info : cameras) {
		auto logger = std::make_shared<Logger>((std::filesystem::temp_directory_path() / "edge-a1-camera.log").string());
		auto camera = std::make_shared<CameraFrameSource>(info.path, info.name, logger, "edge-a1-camera");

		// the widest raw mode that is still quick to process
		CameraMode chosen;
		for (const CameraMode& mode : camera->GetAvailableModes()) {
			if (mode.pixelFormat != FrameFormat::YUYV || mode.width > 1920 || mode.fps < 5) continue;
			if (mode.width > chosen.width) chosen = mode;
		}
		if (chosen.width == 0) continue; // this camera only offers MJPEG
		if (!camera->SetMode(chosen)) continue;
		const CameraMode current = camera->GetCurrentMode();
		INFO(info.name << " at " << current.width << "x" << current.height);

		auto eye = std::make_shared<RoiSource>(logger, "edge-a1-eye", cv::Rect(0, 0, (current.width / 2) & ~1, current.height & ~1));
		REQUIRE(eye->BindSource(camera));
		camera->Toggle(true);
		eye->ISink::Toggle(true);

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (eye->GetCurrentFrameCount() < 2 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(20));
		REQUIRE(eye->GetCurrentFrameCount() >= 2);

		// hold one eye, as StereoPairer / StereoCalibrator / StereoDepthNode do, and keep its pixels as they were when it was taken
		const SourceResult held = eye->GetLatestResult();
		REQUIRE(held.frame.has_value());
		const cv::Mat before = held.frame->AsBgr().clone();
		const uint64_t framesWhenTaken = camera->GetCurrentFrameCount();

		// the camera keeps capturing: with the bug its buffers are recycled into the held eye
		std::this_thread::sleep_for(std::chrono::milliseconds(1500));
		const uint64_t framesLater = camera->GetCurrentFrameCount();
		CHECK(framesLater >= framesWhenTaken + 5);
		CHECK(cv::norm(held.frame->AsBgr(), before, cv::NORM_INF) == 0.0);

		eye->ISink::Toggle(false);
		camera->Toggle(false);
		ran = true;
	}
	if (!ran) SKIP("no attached camera offers a raw (YUYV) mode");
}

// A5 on hardware. A camera that has gone away can keep reporting itself open while every read fails. CaptureFrame then logged an error per
// iteration, with no delay, on a full core, and never reopened it. Here the dequeue of a real camera is made to fail for three seconds
// (the kernel's answer to a camera that was pulled out), then to work again.
extern "C" int edge_v4l2_fail_dequeues(int count); // test_edge_v4l2_camera.cpp's fault injector

TEST_CASE("A5: a camera whose reads keep failing neither spins nor floods the log, and comes back", "[edge][board-media][v4l2][camera]")
{
	const std::vector<EdgeCamera> cameras = EdgeCameras();
	if (cameras.empty()) SKIP("no camera attached");

	const std::filesystem::path logPath = std::filesystem::temp_directory_path() / "edge-a5-camera.log";
	std::filesystem::remove(logPath);
	auto logger = std::make_shared<Logger>(logPath.string());
	auto camera = std::make_shared<CameraFrameSource>(cameras[0].path, cameras[0].name, logger, "edge-a5-camera");
	camera->Toggle(true);

	const auto waitForFrames = [&](uint64_t count, std::chrono::seconds limit) {
		const uint64_t start = camera->GetCurrentFrameCount();
		const auto deadline = std::chrono::steady_clock::now() + limit;
		while (camera->GetCurrentFrameCount() < start + count && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		return camera->GetCurrentFrameCount() >= start + count;
	};
	REQUIRE(waitForFrames(3, std::chrono::seconds(10)));

	const clock_t cpuBefore = clock();
	edge_v4l2_fail_dequeues(INT_MAX);
	std::this_thread::sleep_for(std::chrono::seconds(3));
	const double cpuSeconds = static_cast<double>(clock() - cpuBefore) / CLOCKS_PER_SEC;
	const int failuresLogged = EdgeCountLines(logPath, "camera grab failed");
	edge_v4l2_fail_dequeues(0);

	INFO("grab failures logged in 3 s: " << failuresLogged << ", process CPU time: " << cpuSeconds << " s");
	CHECK(failuresLogged <= 12);   // once, then at most once a second, plus the reopen cycle
	CHECK(cpuSeconds < 2.0);       // a spinning thread alone would use about 3 s of CPU

	// plugged back in: the source reopens the device by itself (retry back-off is up to 10 s) and frames flow again
	CHECK(waitForFrames(3, std::chrono::seconds(30)));
	camera->Toggle(false);
}

// E2 on hardware. Every RecordSink numbered its segments from segment_000000, so a recording started again in the same folder (the service restarted,
// or the user stopped and started it) overwrote the previous session's first segment.
TEST_CASE("E2: a second recording from a camera into the same folder keeps the first one's footage", "[edge][board-media][v4l2][camera]")
{
	const std::vector<EdgeCamera> cameras = EdgeCameras();
	if (cameras.empty()) SKIP("no camera attached");

	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "edge-e2-camera-records";
	std::error_code ignored;
	std::filesystem::remove_all(folder, ignored);
	auto logger = std::make_shared<Logger>((std::filesystem::temp_directory_path() / "edge-e2-camera.log").string());
	auto camera = std::make_shared<CameraFrameSource>(cameras[0].path, cameras[0].name, logger, "edge-e2-camera");
	camera->Toggle(true);

	RecordSinkConfig config;
	config.dstFolder = folder.string();
	config.segmentSeconds = 300;
	auto recordFor = [&](const char* id, std::chrono::milliseconds duration) {
		auto sink = std::make_shared<RecordSink>(logger, id, config);
		REQUIRE(sink->BindSource(camera));
		sink->Toggle(true);
		std::this_thread::sleep_for(duration);
		sink->Toggle(false);
		return sink->ListSegments();
	};

	const std::vector<std::string> first = recordFor("edge-e2-first", std::chrono::seconds(3));
	REQUIRE(first.size() == 1);
	const uintmax_t firstSize = std::filesystem::file_size(folder / first[0]);
	CHECK(firstSize > 0);

	const std::vector<std::string> both = recordFor("edge-e2-second", std::chrono::seconds(3));
	camera->Toggle(false);
	INFO("segments after the second recording: " << both.size());
	CHECK(both.size() == 2);
	CHECK(std::filesystem::exists(folder / first[0]));
	CHECK(std::filesystem::file_size(folder / first[0]) == firstSize); // the first session's file was not overwritten
	std::filesystem::remove_all(folder, ignored);
}

// I2 on hardware: live frames from each camera, through the hardware JPEG encoder at every quality up to the slider's 100. A frame is either
// encoded to a JPEG that decodes (and ends with its EOI marker), or refused so the caller falls back to the CPU encoder; never a broken JPEG.
TEST_CASE("I2: live camera frames at every JPEG quality encode to a decodable JPEG or are refused", "[edge][board-media][v4l2][camera]")
{
	const std::vector<EdgeCamera> cameras = EdgeCameras();
	if (cameras.empty()) SKIP("no camera attached");

	int encoded = 0;
	for (const EdgeCamera& info : cameras) {
		auto logger = std::make_shared<Logger>((std::filesystem::temp_directory_path() / "edge-i2-camera.log").string());
		auto camera = std::make_shared<CameraFrameSource>(info.path, info.name, logger, "edge-i2-camera");
		camera->Toggle(true);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (camera->GetCurrentFrameCount() < 3 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(20));
		REQUIRE(camera->GetCurrentFrameCount() >= 3);

		MjpegHwEncoder encoder;
		for (int frameNumber = 0; frameNumber < 5; frameNumber++) {
			const SourceResult result = camera->GetLatestResult();
			REQUIRE(result.frame.has_value());
			for (int quality : { 50, 90, 97, 98, 99, 100 }) {
				std::vector<uint8_t> jpeg;
				const bool ok = encoder.Encode(*result.frame, quality, 1, jpeg);
				INFO(info.name << " quality " << quality << ": Encode " << (ok ? "true" : "false (" + encoder.LastError() + ")"));
				if (!ok) continue;
				encoded++;
				CHECK(jpeg.size() >= 4);
				CHECK((jpeg[jpeg.size() - 2] == 0xFF && jpeg[jpeg.size() - 1] == 0xD9));
				const cv::Mat decoded = cv::imdecode(jpeg, cv::IMREAD_COLOR);
				CHECK(decoded.size() == result.frame->size());
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(60));
		}
		camera->Toggle(false);
	}
	if (encoded == 0) SKIP("the hardware JPEG encoder refused every live frame (none to check)");
}

#endif // __linux__
