#include <catch2/catch_test_macros.hpp>
#include "Manager.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
}

namespace {

// the position of a four-character MP4 box type in the file's bytes, or npos
size_t FindBox(const std::string& bytes, const char* type)
{
	return bytes.find(type);
}

// What an MP4's first video stream holds, read with libavformat (OpenCV builds without its FFmpeg backend cannot open MP4 at all)
struct Mp4Summary {
	bool opened = false;
	int packets = 0;
	bool firstIsKeyframe = false;
	double firstSeconds = -1;
};

Mp4Summary SummariseMp4(const std::filesystem::path& path)
{
	Mp4Summary summary;
	AVFormatContext* format = nullptr;
	if (avformat_open_input(&format, path.string().c_str(), nullptr, nullptr) < 0) return summary;
	if (avformat_find_stream_info(format, nullptr) >= 0) {
		const int video = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		if (video >= 0) {
			summary.opened = true;
			AVPacket* packet = av_packet_alloc();
			while (av_read_frame(format, packet) >= 0) {
				if (packet->stream_index == video) {
					if (summary.packets == 0) {
						summary.firstIsKeyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
						summary.firstSeconds = packet->pts * av_q2d(format->streams[video]->time_base);
					}
					summary.packets++;
				}
				av_packet_unref(packet);
			}
			av_packet_free(&packet);
		}
	}
	avformat_close_input(&format);
	return summary;
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

TEST_CASE("segments rotate on a keyframe from one running encoder: each file plays from its start and no frame is lost", "[record]") {
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "lumencore-test-record-rotation";
	std::error_code ignored;
	std::filesystem::remove_all(folder, ignored);
	std::filesystem::create_directories(folder);
	const std::filesystem::path clip = std::filesystem::temp_directory_path() / "lumencore-test-record-rotation.avi";
	{
		cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
		REQUIRE_FALSE(bus.empty());
		cv::VideoWriter writer(clip.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 30.0, bus.size());
		REQUIRE(writer.isOpened());
		for (int i = 0; i < 300; i++) writer.write(bus);
	}

	Manager manager;
	int sourceId = manager.CreateVideoFileSource(clip.string(), 30);
	// 1 s segments and a 60-frame GOP: a rotation asks the encoder for a keyframe of its own. libx264 hands it out only after its lookahead
	// (about 60 frames here, more than 3 s at the rate a file source runs in a test), so a segment runs that much over and the 9 s here see
	// one or two rotations; the board's h264_rkmpp hands it out at once.
	int sinkId = manager.CreateRecordSink(folder.string(), "libx264", 2000, 30, 1, 0, 0);
	manager.BindSourceToSink(sourceId, sinkId);
	manager.StartSinkById(sinkId);
	manager.StartSourceById(sourceId);
	std::this_thread::sleep_for(std::chrono::milliseconds(9000));
	manager.StopSourceById(sourceId);
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	manager.StopSinkById(sinkId);

	std::vector<std::filesystem::path> videos;
	for (const auto& entry : std::filesystem::directory_iterator(folder)) {
		if (entry.path().extension() == ".mp4") videos.push_back(entry.path());
	}
	std::sort(videos.begin(), videos.end());

	// every frame the sink encodes gets one line in the sidecar of the segment it was sent in
	int framesSent = 0;
	for (const auto& entry : std::filesystem::directory_iterator(folder)) {
		if (entry.path().extension() != ".jsonl") continue;
		std::ifstream sidecar(entry.path());
		std::string line;
		while (std::getline(sidecar, line)) framesSent++;
	}

	int framesRecorded = 0;
	for (const auto& video : videos) {
		const Mp4Summary summary = SummariseMp4(video);
		UNSCOPED_INFO(video.filename().string() << ": " << std::filesystem::file_size(video) << " bytes, " << summary.packets << " frames");
		REQUIRE(summary.opened);
		// every file starts on a keyframe, so it plays from its start, and its clock starts at 0
		CHECK(summary.firstIsKeyframe);
		CHECK(summary.firstSeconds >= 0.0);
		CHECK(summary.firstSeconds < 0.04);
		framesRecorded += summary.packets;
	}
	CHECK(videos.size() >= 2);
	// the files together hold every frame the encoder was given, none dropped at a rotation
	CHECK(framesSent > 0);
	CHECK(framesRecorded == framesSent);

	manager.Shutdown();
	std::filesystem::remove_all(folder, ignored);
	std::filesystem::remove(clip, ignored);
}
