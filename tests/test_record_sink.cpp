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
	double lastSeconds = -1;
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
					summary.lastSeconds = packet->pts * av_q2d(format->streams[video]->time_base);
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

TEST_CASE("a recording of a fast source plays at the speed it was captured at, not slowed down to the sink's nominal fps", "[record]") {
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "lumencore-test-record-realtime";
	std::error_code ignored;
	std::filesystem::remove_all(folder, ignored);
	std::filesystem::create_directories(folder);
	const std::filesystem::path clip = std::filesystem::temp_directory_path() / "lumencore-test-record-realtime.avi";
	{
		cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
		REQUIRE_FALSE(bus.empty());
		cv::resize(bus, bus, cv::Size(320, 240));
		cv::VideoWriter writer(clip.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 120.0, bus.size());
		REQUIRE(writer.isOpened());
		for (int i = 0; i < 600; i++) writer.write(bus);
	}

	Manager manager;
	int sourceId = manager.CreateVideoFileSource(clip.string(), 120);
	// the sink is told 30 fps (what the server passes whatever the camera does), the source delivers 120
	int sinkId = manager.CreateRecordSink(folder.string(), "libx264", 2000, 30, 60, 0, 0);
	manager.BindSourceToSink(sourceId, sinkId);
	manager.StartSinkById(sinkId);
	const auto started = std::chrono::steady_clock::now();
	manager.StartSourceById(sourceId);
	std::this_thread::sleep_for(std::chrono::milliseconds(2500));
	manager.StopSourceById(sourceId);
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	manager.StopSinkById(sinkId);
	const double wallSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

	std::filesystem::path video;
	for (const auto& entry : std::filesystem::directory_iterator(folder)) {
		if (entry.path().extension() == ".mp4") video = entry.path();
	}
	REQUIRE_FALSE(video.empty());
	const Mp4Summary summary = SummariseMp4(video);
	REQUIRE(summary.opened);
	REQUIRE(summary.packets > 30);
	// the clip lasts about as long as the recording ran; counting frames at 30 fps would make it several times longer
	REQUIRE(summary.lastSeconds <= wallSeconds);
	REQUIRE(summary.lastSeconds > 0.5);

	manager.Shutdown();
	std::filesystem::remove_all(folder, ignored);
	std::filesystem::remove(clip, ignored);
}

// ---- the recording's timing against what the source delivers ----

#include "RecordSink.h"
#include <fstream>
#include <mutex>

namespace {

// a source that publishes exactly the frames it is given, each with a capture time of the caller's choosing
class ScriptedSource : public ISource {
public:
	ScriptedSource(std::shared_ptr<Logger> logger, std::string id) : ISource(logger, id) {}
	void Publish(const cv::Mat& bgr, uint64_t captureUs)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Frame = bgr.clone();
		m_CaptureUs = captureUs;
		m_Pending = true;
	}

protected:
	void CaptureFrame() override
	{
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			if (m_Pending) {
				m_Pending = false;
				SetLatestResult(SourceResult(std::nullopt, Frame(m_Frame), m_CaptureUs));
				return;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

private:
	std::mutex m_Mutex;
	cv::Mat m_Frame;
	uint64_t m_CaptureUs = 0;
	bool m_Pending = false;
};

struct Clip { bool opened = false; int packets = 0; double seconds = 0.0; };

// packets and playing time of the first video stream of an MP4 (last packet's end, from its timestamp and duration)
Clip ReadClip(const std::filesystem::path& path)
{
	Clip clip;
	AVFormatContext* format = nullptr;
	if (avformat_open_input(&format, path.string().c_str(), nullptr, nullptr) < 0) return clip;
	if (avformat_find_stream_info(format, nullptr) >= 0) {
		const int video = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		if (video >= 0) {
			clip.opened = true;
			AVPacket* packet = av_packet_alloc();
			int64_t end = 0;
			while (av_read_frame(format, packet) >= 0) {
				if (packet->stream_index == video) {
					clip.packets++;
					end = std::max(end, packet->pts + packet->duration);
				}
				av_packet_unref(packet);
			}
			av_packet_free(&packet);
			clip.seconds = end * av_q2d(format->streams[video]->time_base);
		}
	}
	avformat_close_input(&format);
	return clip;
}

std::filesystem::path FirstMp4(const std::filesystem::path& folder)
{
	for (const auto& entry : std::filesystem::directory_iterator(folder))
		if (entry.path().extension() == ".mp4") return entry.path();
	return {};
}

}

TEST_CASE("a recording measures the source's frame rate and drops a stale first frame", "[record]") {
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "lumencore-test-record-measured";
	std::error_code ignored;
	std::filesystem::remove_all(folder, ignored);
	std::filesystem::create_directories(folder);
	const std::string logFile = (std::filesystem::temp_directory_path() / "lumencore-test-record-measured.log").string();
	std::filesystem::remove(logFile, ignored);

	auto logger = std::make_shared<Logger>(logFile);
	RecordSinkConfig config;
	config.dstFolder = folder.string();
	config.encoderName = "libx264";
	config.bitrateKbps = 1000;
	config.fps = 30; // the configured rate, which the source does not deliver
	auto sink = std::make_shared<RecordSink>(logger, "measured-sink", config);
	auto source = std::make_shared<ScriptedSource>(logger, "scripted-source");
	REQUIRE(sink->BindSource(source));
	source->Toggle(true);
	static_cast<ISink&>(*sink).Toggle(true);

	cv::Mat picture = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
	REQUIRE_FALSE(picture.empty());
	cv::resize(picture, picture, cv::Size(320, 240));

	// the source's last frame from before the sink started: its capture time is three seconds behind the frames that follow
	const uint64_t start = 50'000'000ull;
	source->Publish(picture, start - 3'000'000ull);
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	// then 90 frames at 90 fps (11.1 ms apart in capture time, 15 ms apart in wall time so the sink keeps up with all of them)
	const int fresh = 90;
	for (int i = 0; i < fresh; i++) {
		source->Publish(picture, start + static_cast<uint64_t>(i) * 1'000'000ull / 90ull);
		std::this_thread::sleep_for(std::chrono::milliseconds(15));
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	static_cast<ISink&>(*sink).Toggle(false);
	source->Toggle(false);

	const std::filesystem::path video = FirstMp4(folder);
	REQUIRE_FALSE(video.empty());
	const Clip clip = ReadClip(video);
	REQUIRE(clip.opened);
	// the stale frame is gone: the clip plays for about the span of the fresh frames (1 s), not three seconds more
	INFO("packets " << clip.packets << ", seconds " << clip.seconds);
	CHECK(clip.packets >= fresh - 10);
	CHECK(clip.packets <= fresh);
	CHECK(clip.seconds > 0.7);
	CHECK(clip.seconds < 1.4);

	// and the encoder was told what the source delivers, not the configured 30 fps
	std::ifstream log(logFile);
	const std::string text((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
	INFO(text);
	CHECK(text.find("the source delivers 9") != std::string::npos); // 90 fps, measured as 90.0 (or a hair under)

	sink.reset();
	source.reset();
	std::filesystem::remove_all(folder, ignored);
}

TEST_CASE("a recording of one still picture still produces a file", "[record]") {
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "lumencore-test-record-still";
	std::error_code ignored;
	std::filesystem::remove_all(folder, ignored);
	std::filesystem::create_directories(folder);

	auto logger = std::make_shared<Logger>((std::filesystem::temp_directory_path() / "lumencore-test-record-still.log").string());
	RecordSinkConfig config;
	config.dstFolder = folder.string();
	config.encoderName = "libx264";
	auto sink = std::make_shared<RecordSink>(logger, "still-sink", config);
	auto source = std::make_shared<ScriptedSource>(logger, "still-source");
	REQUIRE(sink->BindSource(source));
	source->Toggle(true);
	static_cast<ISink&>(*sink).Toggle(true);

	cv::Mat picture = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
	REQUIRE_FALSE(picture.empty());
	cv::resize(picture, picture, cv::Size(320, 240));
	source->Publish(picture, 60'000'000ull);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	static_cast<ISink&>(*sink).Toggle(false); // fewer than two frames ever arrived: they are written when the recording stops
	source->Toggle(false);

	const std::filesystem::path video = FirstMp4(folder);
	REQUIRE_FALSE(video.empty());
	const Clip clip = ReadClip(video);
	REQUIRE(clip.opened);
	CHECK(clip.packets == 1);

	sink.reset();
	source.reset();
	std::filesystem::remove_all(folder, ignored);
}
