#include <catch2/catch_test_macros.hpp>
#include "Manager.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

namespace {

// the position of a four-character MP4 box type in the file's bytes, or npos
size_t FindBox(const std::string& bytes, const char* type)
{
	return bytes.find(type);
}

}

TEST_CASE("a recording is a fragmented MP4, so a power cut keeps everything up to the last keyframe", "[record]") {
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "lumencore-test-record-fragmented";
	std::error_code ignored;
	std::filesystem::remove_all(folder, ignored);
	std::filesystem::create_directories(folder);
	const std::filesystem::path clip = std::filesystem::temp_directory_path() / "lumencore-test-record-source.avi";
	{
		cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
		REQUIRE_FALSE(bus.empty());
		cv::VideoWriter writer(clip.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 30.0, bus.size());
		REQUIRE(writer.isOpened());
		for (int i = 0; i < 300; i++) writer.write(bus);
	}

	Manager manager;
	int sourceId = manager.CreateVideoFileSource(clip.string(), 30);
	int sinkId = manager.CreateRecordSink(folder.string(), "libx264", 2000, 30, 60, 0, 0);
	manager.BindSourceToSink(sourceId, sinkId);
	manager.StartSinkById(sinkId);
	manager.StartSourceById(sourceId);
	std::this_thread::sleep_for(std::chrono::milliseconds(2500));
	manager.StopSinkById(sinkId); // closes the open segment
	manager.StopSourceById(sourceId);

	std::filesystem::path video;
	for (const auto& entry : std::filesystem::directory_iterator(folder)) {
		if (entry.path().extension() == ".mp4") video = entry.path();
	}
	REQUIRE_FALSE(video.empty());
	std::ifstream file(video, std::ios::binary);
	const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	// the movie header comes first (empty_moov) and the frames follow in movie fragments
	const size_t moov = FindBox(bytes, "moov");
	const size_t moof = FindBox(bytes, "moof");
	REQUIRE(moov != std::string::npos);
	REQUIRE(moof != std::string::npos);
	REQUIRE(moov < moof);

	manager.Shutdown();
	file.close();
	std::filesystem::remove_all(folder, ignored);
	std::filesystem::remove(clip, ignored);
}
