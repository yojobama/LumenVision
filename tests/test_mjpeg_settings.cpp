#include <catch2/catch_test_macros.hpp>
#include "Manager.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <nlohmann/json.hpp>
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

// how many JPEGs the sink has encoded so far, from its stage profile
int EncodedFrames(Manager& manager, int sinkId)
{
	nlohmann::json all = nlohmann::json::parse(manager.GetNodeProfilesJson());
	const std::string key = std::to_string(sinkId);
	if (!all.contains(key)) return 0;
	for (const auto& stage : all[key]) {
		if (stage["stage"] == "jpeg") return stage["count"].get<int>();
	}
	return 0;
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

TEST_CASE("Shutdown stops and destroys every running node and may be called twice", "[manager][shutdown]") {
	Manager manager;
	const std::filesystem::path clip = std::filesystem::temp_directory_path() / "lumencore-test-shutdown.avi";
	{
		cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
		REQUIRE_FALSE(bus.empty());
		cv::VideoWriter writer(clip.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 30.0, bus.size());
		REQUIRE(writer.isOpened());
		for (int i = 0; i < 300; i++) writer.write(bus);
	}
	int sourceId = manager.CreateVideoFileSource(clip.string(), 30);
	int detectorId = manager.CreateApriltagDetector();
	int sinkId = manager.CreateMjpegSink(80);
	manager.BindSourceToSink(sourceId, detectorId);
	manager.BindSourceToSink(detectorId, sinkId);
	manager.StartSinkById(sinkId);
	manager.StartSinkById(detectorId);
	manager.StartSourceById(sourceId);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	manager.Shutdown();

	REQUIRE(manager.GetAllSinks().empty());
	REQUIRE(manager.GetAllSources().empty());
	manager.Shutdown();
	std::error_code ignored;
	std::filesystem::remove(clip, ignored);
}

TEST_CASE("an MjpegSink encodes only while its frames are being polled", "[mjpeg]") {
	Manager manager;
	const std::filesystem::path clip = std::filesystem::temp_directory_path() / "lumencore-test-mjpeg-lazy.avi";
	{
		cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
		REQUIRE_FALSE(bus.empty());
		cv::VideoWriter writer(clip.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 30.0, bus.size());
		REQUIRE(writer.isOpened());
		for (int i = 0; i < 900; i++) writer.write(bus);
	}
	int sourceId = manager.CreateVideoFileSource(clip.string(), 30);
	int sinkId = manager.CreateMjpegSink(80);
	manager.BindSourceToSink(sourceId, sinkId);
	manager.StartSinkById(sinkId);
	manager.StartSourceById(sourceId);

	// polled: frames keep being encoded, at the poll rate rather than the 30 fps the clip plays at
	WaitForFrameSize(manager, sinkId, cv::Size());
	const int polledStart = EncodedFrames(manager, sinkId);
	for (int i = 0; i < 20; i++) {
		manager.GetMjpegFrameBase64(sinkId);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	const int polledEncodes = EncodedFrames(manager, sinkId) - polledStart;
	REQUIRE(polledEncodes >= 1); // a sanitiser build on a slow board encodes only a few a second; the bound below is what shows it is the poll rate
	REQUIRE(polledEncodes <= 14); // ~1 s of polling at one encode per 80 ms at most

	// unpolled: after the watched window runs out nothing is encoded
	std::this_thread::sleep_for(std::chrono::milliseconds(2200));
	const int idleStart = EncodedFrames(manager, sinkId);
	std::this_thread::sleep_for(std::chrono::milliseconds(600));
	REQUIRE(EncodedFrames(manager, sinkId) == idleStart);

	// polling again wakes it
	manager.GetMjpegFrameBase64(sinkId);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	REQUIRE(EncodedFrames(manager, sinkId) > idleStart);

	manager.StopSourceById(sourceId);
	manager.Shutdown();
	std::error_code ignored;
	std::filesystem::remove(clip, ignored);
}

TEST_CASE("the server's copy of an MjpegSink frame is the JPEG itself, sized first when the buffer is short, with a sequence that advances", "[mjpeg]") {
	Manager manager;
	const std::filesystem::path clip = std::filesystem::temp_directory_path() / "lumencore-test-mjpeg-copy.avi";
	{
		cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
		REQUIRE_FALSE(bus.empty());
		cv::VideoWriter writer(clip.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 30.0, bus.size());
		REQUIRE(writer.isOpened());
		for (int i = 0; i < 300; i++) writer.write(bus);
	}
	int sourceId = manager.CreateVideoFileSource(clip.string(), 30);
	int sinkId = manager.CreateMjpegSink(80);
	manager.BindSourceToSink(sourceId, sinkId);
	manager.StartSinkById(sinkId);
	manager.StartSourceById(sourceId);

	REQUIRE(manager.CopyMjpegFrame(sinkId, nullptr, 0) >= 0);
	REQUIRE(WaitForFrameSize(manager, sinkId, cv::Size()) != cv::Size());
	const long long first = manager.GetMjpegFrameSequence(sinkId);
	REQUIRE(first > 0);

	// a short buffer: the size comes back and nothing is written
	std::vector<uint8_t> small(16, 0xAB);
	const int size = manager.CopyMjpegFrame(sinkId, small.data(), static_cast<int>(small.size()));
	REQUIRE(size > 16);
	REQUIRE(small == std::vector<uint8_t>(16, 0xAB));

	// a big enough buffer: a JPEG that decodes to the source picture's size
	std::vector<uint8_t> jpeg(static_cast<size_t>(size) * 2);
	const int copied = manager.CopyMjpegFrame(sinkId, jpeg.data(), static_cast<int>(jpeg.size()));
	REQUIRE(copied > 0);
	jpeg.resize(static_cast<size_t>(copied));
	REQUIRE(jpeg[0] == 0xFF);
	REQUIRE(jpeg[1] == 0xD8);
	cv::Mat decoded = cv::imdecode(jpeg, cv::IMREAD_COLOR);
	REQUIRE(decoded.size() == cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg").size());

	// still polled, so it keeps encoding and the sequence moves on
	for (int i = 0; i < 20 && manager.GetMjpegFrameSequence(sinkId) == first; i++) {
		manager.CopyMjpegFrame(sinkId, nullptr, 0);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	REQUIRE(manager.GetMjpegFrameSequence(sinkId) > first);

	// not an MjpegSink: nothing
	REQUIRE(manager.CopyMjpegFrame(sourceId, jpeg.data(), static_cast<int>(jpeg.size())) == 0);
	REQUIRE(manager.GetMjpegFrameSequence(-1) == 0);
	manager.Shutdown();
	std::error_code ignored;
	std::filesystem::remove(clip, ignored);
}
