// Edge-case hunt, area A (frames and sources). Each test asserts the correct behaviour, so it fails on the code as it is today.
#include <catch2/catch_test_macros.hpp>
#include "CameraCalibrationResult.h"
#include "CameraSource.h"
#include "Frame.h"
#include "FramePool.h"
#include "FrameTransform.h"
#include "RoiSource.h"
#include "VideoFileSource.h"
#include <opencv2/opencv.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::filesystem::path EdgeTempPath(const std::string& name)
{
	return std::filesystem::temp_directory_path() / ("lumen-edge-frames-" + name);
}

std::string ReadWholeFile(const std::filesystem::path& path)
{
	std::ifstream file(path);
	return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

size_t CountOccurrences(const std::string& haystack, const std::string& needle)
{
	size_t count = 0;
	for (size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) count++;
	return count;
}

// A short MJPG .avi whose frame i is a flat grey of value 10 * i (OpenCV's built-in MJPEG writer, no codec needed); empty path on failure.
std::string WriteNumberedVideo(const std::string& name, int frames)
{
	const std::filesystem::path path = EdgeTempPath(name + ".avi");
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
	cv::VideoWriter writer(path.string(), cv::CAP_OPENCV_MJPEG, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 30.0, cv::Size(64, 48), true);
	if (!writer.isOpened()) return {};
	for (int i = 0; i < frames; i++) writer.write(cv::Mat(48, 64, CV_8UC3, cv::Scalar::all(10 * i)));
	writer.release();
	cv::VideoCapture check(path.string());
	return check.isOpened() ? path.string() : std::string();
}

// A source whose frames live in FramePool buffers, as a V4L2 camera's BGR/grey frames do (V4l2CameraBackend::Grab).
class PooledGraySource : public ISource
{
public:
	PooledGraySource(std::shared_ptr<Logger> logger, std::string id, int width, int height)
		: ISource(logger, id), m_Width(width), m_Height(height)
	{
		m_DoNotLoadCaptureThread = true;
	}
	void Publish(uint8_t value)
	{
		std::shared_ptr<void> owner;
		cv::Mat mat = FramePool::Instance().Acquire(m_Height, m_Width, CV_8UC1, owner);
		mat.setTo(value);
		SetLatestResult(SourceResult(std::nullopt, Frame(mat, FrameFormat::GRAY8, owner), 1000 + value));
	}

private:
	int m_Width, m_Height;
};

bool WaitFor(const std::function<bool()>& condition, std::chrono::milliseconds timeout)
{
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		if (condition()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return condition();
}

}

// ---------------------------------------------------------------------------------------------------------------------------------------
// A1: Frame::Roi of a pooled (non-NV12) frame drops the pool owner, so the crop's pixels are recycled under it.

TEST_CASE("A: a crop of a pooled grey frame keeps its pixels after the parent frame is released", "[edge][frames]") {
	// an odd, otherwise unused shape so this test's free list is its own
	const int rows = 37, cols = 53;
	Frame crop;
	{
		std::shared_ptr<void> owner;
		cv::Mat mat = FramePool::Instance().Acquire(rows, cols, CV_8UC1, owner);
		mat.setTo(11);
		Frame parent(mat, FrameFormat::GRAY8, owner);
		crop = parent.Roi(cv::Rect(0, 0, cols / 2, rows));
	}
	// the parent and its owner are gone: the next camera frame of this shape is acquired and written
	std::shared_ptr<void> nextOwner;
	cv::Mat next = FramePool::Instance().Acquire(rows, cols, CV_8UC1, nextOwner);
	next.setTo(222);

	REQUIRE(crop.AsGray().at<uint8_t>(rows / 2, 3) == 11);
}

TEST_CASE("A: a crop of a pooled BGR frame keeps its pixels after the parent frame is released", "[edge][frames]") {
	const int rows = 39, cols = 51;
	Frame crop;
	{
		std::shared_ptr<void> owner;
		cv::Mat mat = FramePool::Instance().Acquire(rows, cols, CV_8UC3, owner);
		mat.setTo(cv::Scalar(1, 2, 3));
		Frame parent(mat, FrameFormat::BGR24, owner);
		crop = parent.Roi(cv::Rect(cols / 2, 0, cols - cols / 2, rows));
	}
	std::shared_ptr<void> nextOwner;
	cv::Mat next = FramePool::Instance().Acquire(rows, cols, CV_8UC3, nextOwner);
	next.setTo(cv::Scalar(200, 201, 202));

	REQUIRE(crop.AsBgr().at<cv::Vec3b>(rows / 2, 3) == cv::Vec3b(1, 2, 3));
}

TEST_CASE("A: RoiSource eyes of a pooled camera frame are not overwritten by later camera frames", "[edge][frames]") {
	auto logger = std::make_shared<Logger>(EdgeTempPath("roi-pool.log").string());
	const int width = 2 * 41, height = 29; // a mono side-by-side camera, odd shape so the pool bucket is this test's
	auto upstream = std::make_shared<PooledGraySource>(logger, "edge-a-pooled-upstream", width, height);
	RoiSource left(logger, "edge-a-pooled-left", cv::Rect(0, 0, width / 2, height));
	REQUIRE(left.BindSource(upstream));
	static_cast<ISink&>(left).Toggle(true);

	upstream->Publish(1);
	REQUIRE(WaitFor([&] { return left.GetCurrentFrameCount() >= 1; }, std::chrono::seconds(2)));
	SourceResult first = left.GetLatestResult();
	REQUIRE(first.frame.has_value());
	REQUIRE(first.frame->AsGray().at<uint8_t>(height / 2, 5) == 1);

	// the camera keeps capturing; frame 1's buffer goes back to the pool once the camera's own latest result moves on
	for (uint8_t v = 2; v <= 6; v++) {
		const uint64_t before = left.GetCurrentFrameCount();
		upstream->Publish(v);
		REQUIRE(WaitFor([&] { return left.GetCurrentFrameCount() > before; }, std::chrono::seconds(2)));
	}
	static_cast<ISink&>(left).Toggle(false);

	// the stereo pairer / calibrator still holds the first left eye: it must still show frame 1
	CHECK(first.captureTimeUs == 1001);
	REQUIRE(static_cast<int>(first.frame->AsGray().at<uint8_t>(height / 2, 5)) == 1);
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// A2: a crop that no longer overlaps the frame (the camera's mode was lowered after the crop was set) makes Apply() skip the rotation and
// flips and return the raw frame, while OutputSize() and TransformCalibration() describe a 0x0 picture with a moved principal point.

TEST_CASE("A: a crop outside the frame still rotates, and the reported size and calibration match the frames", "[edge][frames]") {
	FrameTransform transform;
	transform.rotation = 90;
	transform.cropX = 700; // set at 1280x720, camera since switched to 640x480
	transform.cropY = 0;
	transform.cropWidth = 400;
	transform.cropHeight = 300;
	REQUIRE_FALSE(transform.IsIdentity());

	cv::Mat image(480, 640, CV_8UC1, cv::Scalar(0));
	image.at<uint8_t>(0, 0) = 255; // top-left corner marker
	bool view = false;
	cv::Mat out = transform.Apply(image, view);

	// whatever Apply() makes of the crop, OutputSize() must describe it and the calibration must be for it
	CHECK(transform.OutputSize(image.size()) == out.size());
	CameraCalibrationResult calibration(500, 500, 320, 240, 0.2, { 0.1, 0.01, 0.001, 0.002, 0 }, 640, 480);
	CameraCalibrationResult moved = TransformCalibration(calibration, transform);
	CHECK(cv::Size(moved.imageWidth, moved.imageHeight) == out.size());
	CHECK(moved.cx >= 0.0);
	CHECK(moved.cy >= 0.0);
	// the 90 degree turn the user asked for must still happen
	REQUIRE(out.size() == cv::Size(480, 640));
	REQUIRE(out.at<uint8_t>(0, out.cols - 1) == 255); // clockwise: top-left goes to top-right
}

TEST_CASE("A: a crop outside the frame still applies the requested mirror", "[edge][frames]") {
	FrameTransform transform;
	transform.flipHorizontal = true;
	transform.cropX = 1000;
	transform.cropY = 1000;
	transform.cropWidth = 100;
	transform.cropHeight = 100;

	cv::Mat image(48, 64, CV_8UC1, cv::Scalar(0));
	image.at<uint8_t>(0, 0) = 255;
	bool view = true;
	cv::Mat out = transform.Apply(image, view);
	REQUIRE(transform.OutputSize(image.size()) == out.size());
	REQUIRE(out.at<uint8_t>(0, out.cols - 1) == 255);
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// A3: VideoFileFrameSource::CaptureFrame calls grab() and then read() (which grabs again), so every other frame of a video is dropped.

TEST_CASE("A: a video file source publishes every frame of the file, in order", "[edge][frames]") {
	const std::string path = WriteNumberedVideo("numbered", 12);
	if (path.empty()) SKIP("no OpenCV video writer/reader for MJPG .avi on this build");
	auto logger = std::make_shared<Logger>(EdgeTempPath("video-order.log").string());

	auto source = std::make_shared<VideoFileFrameSource>(logger, path, 200, "edge-a-video-order");
	std::mutex seenMutex;
	std::vector<int> seen;
	source->AddResultListener([&] {
		SourceResult result = source->GetLatestResult();
		if (!result.frame.has_value() || result.frame->empty()) return;
		const int index = static_cast<int>(cv::mean(result.frame->AsGray())[0] / 10.0 + 0.5);
		std::lock_guard<std::mutex> lock(seenMutex);
		seen.push_back(index);
	});
	source->Toggle(true);
	WaitFor([&] { std::lock_guard<std::mutex> lock(seenMutex); return seen.size() >= 6; }, std::chrono::seconds(3));
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	source->Toggle(false);

	std::vector<int> firstSix;
	{
		std::lock_guard<std::mutex> lock(seenMutex);
		firstSix.assign(seen.begin(), seen.begin() + std::min<size_t>(6, seen.size()));
	}
	INFO("frame indices published: " << ::Catch::Detail::stringify(firstSix));
	REQUIRE(firstSix == std::vector<int>{ 0, 1, 2, 3, 4, 5 });
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// A4: at the end of a video file, VideoFileFrameSource neither loops nor stops: it logs an error on every frame interval, forever.

TEST_CASE("A: a video file that has ended does not log an error on every frame interval", "[edge][frames]") {
	const std::string path = WriteNumberedVideo("short", 3);
	if (path.empty()) SKIP("no OpenCV video writer/reader for MJPG .avi on this build");
	const std::filesystem::path logPath = EdgeTempPath("video-eof.log");
	std::error_code ignored;
	std::filesystem::remove(logPath, ignored);
	{
		auto logger = std::make_shared<Logger>(logPath.string());
		VideoFileFrameSource source(logger, path, 100, "edge-a-video-eof");
		source.Toggle(true);
		std::this_thread::sleep_for(std::chrono::milliseconds(1500));
		source.Toggle(false);
		logger->FlushLogs();
	}
	const std::string log = ReadWholeFile(logPath);
	const size_t failures = CountOccurrences(log, "Failed to read frame from video file");
	INFO("end-of-file errors logged in 1.5 s: " << failures);
	REQUIRE(failures <= 1);
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// A5: a camera whose reads start failing while the device stays "open" (a webcam unplugged under MSMF, a USB camera unplugged under V4L2:
// poll() reports POLLERR at once and DQBUF fails) makes CameraFrameSource spin with no delay, logging an error per iteration, and it never
// reaches the reopen path that test_camera_closed.cpp covers. A video file through the OpenCV backend reproduces "read fails, still open".

TEST_CASE("A: a camera whose reads keep failing does not spin and flood the log", "[edge][frames]") {
#ifndef _WIN32
	SKIP("the OpenCV camera backend opens files only on Windows (Linux uses CAP_V4L2)");
#else
	const std::string path = WriteNumberedVideo("camera-eof", 2);
	if (path.empty()) SKIP("no OpenCV video writer/reader for MJPG .avi on this build");
	const std::filesystem::path logPath = EdgeTempPath("camera-eof.log");
	std::error_code ignored;
	std::filesystem::remove(logPath, ignored);
	{
		auto logger = std::make_shared<Logger>(logPath.string());
		CameraFrameSource source(path, "unplugged camera", logger, "edge-a-camera-eof");
		source.Toggle(true);
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		source.Toggle(false);
		logger->FlushLogs();
	}
	const std::string log = ReadWholeFile(logPath);
	const size_t failures = CountOccurrences(log, "camera grab failed");
	INFO("grab failures logged in 1 s: " << failures << " (log " << log.size() << " bytes)");
	REQUIRE(failures <= 10);
#endif
}

