#include <catch2/catch_test_macros.hpp>
#include "Manager.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <filesystem>
#include <chrono>
#include <thread>

namespace {

std::vector<uint8_t> DecodeBase64(const std::string& text)
{
	static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::vector<uint8_t> out;
	uint32_t buffer = 0;
	int bits = 0;
	for (char c : text) {
		if (c == '=') break;
		buffer = (buffer << 6) | static_cast<uint32_t>(alphabet.find(c));
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
		}
	}
	return out;
}

// the size of the newest frame the sink has produced, once it differs from `previous` (or after a timeout)
cv::Size WaitForFrameSize(Manager& manager, int sinkId, cv::Size previous)
{
	for (int attempt = 0; attempt < 200; attempt++) {
		std::string base64 = manager.GetMjpegFrameBase64(sinkId);
		if (!base64.empty()) {
			cv::Mat decoded = cv::imdecode(DecodeBase64(base64), cv::IMREAD_COLOR);
			if (!decoded.empty() && decoded.size() != previous) return decoded.size();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	}
	return previous;
}

}

TEST_CASE("an MjpegSink's size divisor and quality can be changed while it runs", "[mjpeg]") {
	Manager manager;
	// a still image publishes once, so the sink needs a source that keeps producing frames: a short MJPEG clip of the bus picture
	const std::filesystem::path clip = std::filesystem::temp_directory_path() / "lumencore-test-mjpeg-settings.avi";
	{
		cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
		REQUIRE_FALSE(bus.empty());
		cv::VideoWriter writer(clip.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 30.0, bus.size());
		REQUIRE(writer.isOpened());
		for (int i = 0; i < 600; i++) writer.write(bus);
	}
	int sourceId = manager.CreateVideoFileSource(clip.string(), 30);
	int sinkId = manager.CreateMjpegSink(80);
	manager.BindSourceToSink(sourceId, sinkId);
	manager.StartSinkById(sinkId);
	manager.StartSourceById(sourceId);

	cv::Size full = WaitForFrameSize(manager, sinkId, cv::Size());
	REQUIRE(full == cv::Size(640, 640));

	manager.SetMjpegSinkSettings(sinkId, 40, 2);
	REQUIRE(WaitForFrameSize(manager, sinkId, full) == cv::Size(320, 320));

	manager.SetMjpegSinkSettings(sinkId, 40, 1);
	REQUIRE(WaitForFrameSize(manager, sinkId, cv::Size(320, 320)) == cv::Size(640, 640));

	manager.StopSourceById(sourceId);
	std::error_code ignored; // the source may still hold the file open on Windows
	std::filesystem::remove(clip, ignored);
}

TEST_CASE("changing the settings of something that is not an MjpegSink throws", "[mjpeg]") {
	Manager manager;
	int detectorId = manager.CreateApriltagDetector();
	REQUIRE_THROWS(manager.SetMjpegSinkSettings(detectorId, 50, 1));
	REQUIRE_THROWS(manager.SetMjpegSinkSettings(123456, 50, 1));
}
