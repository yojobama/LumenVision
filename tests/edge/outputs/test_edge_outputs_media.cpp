// Edge-case hunt, area E (outputs, encoding side): RecordSink, WebRTCSink and MjpegSink.
#include <catch2/catch_test_macros.hpp>
#include "MjpegSink.h"
#include "edge_outputs_access.h"
#include <opencv2/imgproc.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef LUMEN_WITH_RECORD
#include "RecordSink.h"
extern "C" {
#include <libavformat/avformat.h>
}
#endif
#ifdef LUMEN_WITH_WEBRTC
#include "WebRTCSink.h"
#endif

struct EdgeOutputsMjpegProcessTag {
	using type = void (MjpegSink::*)(const std::vector<SourceResult>&);
	friend type get(EdgeOutputsMjpegProcessTag);
};
template struct EdgeOutputsRob<EdgeOutputsMjpegProcessTag, &MjpegSink::Process>;

#ifdef LUMEN_WITH_RECORD
struct EdgeOutputsRecordProcessTag {
	using type = void (RecordSink::*)(const std::vector<SourceResult>&);
	friend type get(EdgeOutputsRecordProcessTag);
};
template struct EdgeOutputsRob<EdgeOutputsRecordProcessTag, &RecordSink::Process>;
#endif

#ifdef LUMEN_WITH_WEBRTC
struct EdgeOutputsWebRtcEncoderTag {
	using type = bool (WebRTCSink::*)(int, int);
	friend type get(EdgeOutputsWebRtcEncoderTag);
};
template struct EdgeOutputsRob<EdgeOutputsWebRtcEncoderTag, &WebRTCSink::EnsureEncoderInitialized>;
#endif

namespace {
// a moving test picture, so the encoder has something to code
cv::Mat EdgeOutputsPicture(int width, int height, int index)
{
	cv::Mat image(height, width, CV_8UC3, cv::Scalar(40, 90, 160));
	cv::rectangle(image, cv::Rect((index * 7) % std::max(1, width - 20), height / 4, 20, height / 2), cv::Scalar(250, 250, 250), cv::FILLED);
	return image;
}

SourceResult EdgeOutputsFrameResult(const cv::Mat& image, uint64_t frameNumber)
{
	SourceResult result(nlohmann::json{ {"frame", frameNumber} }, Frame(image), SourceResult::NowUs());
	result.sourceId = "camera";
	result.frameNumber = frameNumber;
	return result;
}

#ifdef LUMEN_WITH_RECORD
int EdgeOutputsCountMp4Packets(const std::filesystem::path& path)
{
	AVFormatContext* format = nullptr;
	if (avformat_open_input(&format, path.string().c_str(), nullptr, nullptr) < 0) return -1;
	int packets = 0;
	if (avformat_find_stream_info(format, nullptr) >= 0) {
		AVPacket* packet = av_packet_alloc();
		while (av_read_frame(format, packet) >= 0) { packets++; av_packet_unref(packet); }
		av_packet_free(&packet);
	}
	avformat_close_input(&format);
	return packets;
}

// every frameNumber written to the folder's sidecars
std::vector<uint64_t> EdgeOutputsSidecarFrames(const std::filesystem::path& folder)
{
	std::vector<uint64_t> frames;
	for (const auto& entry : std::filesystem::directory_iterator(folder)) {
		if (entry.path().extension() != ".jsonl") continue;
		std::ifstream sidecar(entry.path());
		std::string line;
		while (std::getline(sidecar, line)) {
			nlohmann::json record = nlohmann::json::parse(line, nullptr, false);
			if (!record.is_discarded()) frames.push_back(record["frameNumber"].get<uint64_t>());
		}
	}
	return frames;
}

std::vector<std::string> EdgeOutputsMp4Names(const std::filesystem::path& folder)
{
	std::vector<std::string> names;
	for (const auto& entry : std::filesystem::directory_iterator(folder))
		if (entry.path().extension() == ".mp4") names.push_back(entry.path().filename().string());
	std::sort(names.begin(), names.end());
	return names;
}

// One recording session: a fresh RecordSink on `folder` (as after a service restart, or a sink rebuilt from the saved settings) records
// `count` frames numbered from `firstFrame`, then is destroyed, which closes its segment.
void EdgeOutputsRecordSession(const std::filesystem::path& folder, uint64_t firstFrame, int count, int width, int height, int maxFileCount = 0)
{
	auto logger = std::make_shared<Logger>("LumenCoreTests-edge-outputs-record.log");
	RecordSinkConfig config;
	config.dstFolder = folder.string();
	config.encoderName = "libx264";
	config.bitrateKbps = 1000;
	config.fps = 30;
	config.segmentSeconds = 300;
	config.maxFileCount = maxFileCount;
	auto sink = std::make_shared<RecordSink>(logger, "edge-outputs-record", config);
	auto process = get(EdgeOutputsRecordProcessTag{});
	for (int i = 0; i < count; i++)
		(sink.get()->*process)(std::vector<SourceResult>{ EdgeOutputsFrameResult(EdgeOutputsPicture(width, height, i), firstFrame + i) });
	sink.reset();
}
#endif
}

#ifdef LUMEN_WITH_RECORD
// Server/SinkManager restores a persisted RecordSink with the same dstFolder on every start, and RecordSink numbers its segments from
// m_SegmentIndex = 0 in each new instance. The first segment after a restart is therefore segment_000000.mp4/.jsonl again, opened with
// truncation: the previous session's footage (the last match, as the robot is power-cycled between matches) is overwritten.
TEST_CASE("E: a second recording session into the same folder keeps the first session's footage", "[edge][outputs]") {
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "lumencore-edge-outputs-record-restart";
	std::error_code ignored;
	std::filesystem::remove_all(folder, ignored);
	std::filesystem::create_directories(folder);

	EdgeOutputsRecordSession(folder, 1, 40, 320, 240);      // match 1
	const std::vector<std::string> afterFirst = EdgeOutputsMp4Names(folder);
	REQUIRE(afterFirst.size() == 1);
	EdgeOutputsRecordSession(folder, 1001, 40, 320, 240);   // the service restarts, match 2

	const std::vector<uint64_t> frames = EdgeOutputsSidecarFrames(folder);
	const std::vector<std::string> videos = EdgeOutputsMp4Names(folder);
	std::string list;
	for (const auto& v : videos) list += v + " ";
	UNSCOPED_INFO("segments after two sessions: " << list);
	const bool firstSessionKept = std::find(frames.begin(), frames.end(), uint64_t{ 1 }) != frames.end();
	const bool secondSessionKept = std::find(frames.begin(), frames.end(), uint64_t{ 1001 }) != frames.end();
	CHECK(videos.size() == 2);
	CHECK(firstSessionKept);
	CHECK(secondSessionKept);
	std::filesystem::remove_all(folder, ignored);
}

// The same restart with a file-count cap: EnforceRetention assumes the segment being written sorts last ("never evict the segment being
// written: it is the last entry after the ascending sort"), but a restarted sink writes segment_000000 while the previous session left
// higher numbers behind, so the active segment sorts FIRST and is the one evicted. On Linux remove() unlinks the open file and the whole
// new recording is lost; the older files are deleted too.
TEST_CASE("E: with a file-count cap, a restarted recording does not delete the segment it is writing", "[edge][outputs]") {
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "lumencore-edge-outputs-record-retention";
	std::error_code ignored;
	std::filesystem::remove_all(folder, ignored);
	std::filesystem::create_directories(folder);
	// what a previous session with maxFileCount 2 leaves behind after it rotated through segments 0..8
	for (const char* name : { "segment_000007", "segment_000008" }) {
		std::ofstream(folder / (std::string(name) + ".mp4"), std::ios::binary) << "old footage";
		std::ofstream(folder / (std::string(name) + ".jsonl")) << "{\"frameNumber\":7}\n";
	}

	EdgeOutputsRecordSession(folder, 2001, 40, 320, 240, 2 /* maxFileCount */);

	const std::vector<uint64_t> frames = EdgeOutputsSidecarFrames(folder);
	const std::vector<std::string> videos = EdgeOutputsMp4Names(folder);
	std::string list;
	for (const auto& v : videos) list += v + " ";
	UNSCOPED_INFO("segments after the restarted session: " << list);
	const bool newSessionKept = std::find(frames.begin(), frames.end(), uint64_t{ 2001 }) != frames.end();
	CHECK(newSessionKept);
	CHECK(videos.size() == 2);
	std::filesystem::remove_all(folder, ignored);
}

// RecordSink encodes BGR frames at their own size into YUV420P; only the NV12 path rounds the size down to even. A BGR frame with an odd
// width or height (a crop the user drew 641 px wide, or a detector's annotated output of such a crop) makes libx264's avcodec_open2 fail
// ("width not divisible by 2"), and since the failure is retried on every frame, the recording silently stays empty (an error log line,
// and a new encoder allocation, per frame).
TEST_CASE("E: an odd-sized BGR frame is still recorded", "[edge][outputs]") {
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "lumencore-edge-outputs-record-odd";
	std::error_code ignored;
	std::filesystem::remove_all(folder, ignored);
	std::filesystem::create_directories(folder);

	EdgeOutputsRecordSession(folder, 1, 40, 641, 481);

	const std::vector<std::string> videos = EdgeOutputsMp4Names(folder);
	CHECK(videos.size() == 1);
	int packets = 0;
	for (const auto& v : videos) packets += std::max(0, EdgeOutputsCountMp4Packets(folder / v));
	CHECK(packets > 0);
	CHECK(EdgeOutputsSidecarFrames(folder).size() == 40);
	std::filesystem::remove_all(folder, ignored);
}
#endif // LUMEN_WITH_RECORD

#ifdef LUMEN_WITH_WEBRTC
// WebRTCSink scales a BGR frame (a detector's annotated output, or any camera frame on a desktop) with cv::resize(1/N) and hands the result's
// size to the encoder as it is; only the NV12 path rounds to even. The web UI offers 1/3 and 1/6: a 1280x720 frame at 1/3 is 427x240 and at
// 1/6 213x120, so libx264 (YUV420P) refuses to open, EnsureEncoderInitialized fails on every frame and the viewer gets no video at all.
TEST_CASE("E: a WebRTC stream of a 1280x720 BGR frame at the UI's 1/3 size can open its encoder", "[edge][outputs]") {
	auto logger = std::make_shared<Logger>("LumenCoreTests-edge-outputs-webrtc.log");
	WebRTCSinkConfig config;
	config.encoderName = "libx264";
	auto sink = std::make_shared<WebRTCSink>(logger, "edge-outputs-webrtc", config);
	auto ensureEncoder = get(EdgeOutputsWebRtcEncoderTag{});

	// the size the sink encodes a 1280x720 BGR frame at, for each of the UI's divisors
	for (int divisor : { 1, 2, 3, 6 }) {
		const cv::Size size = WebRTCSink::EncodedSizeFor(cv::Size(1280, 720), divisor);
		INFO("divisor " << divisor << " -> " << size.width << "x" << size.height);
		CHECK((sink.get()->*ensureEncoder)(size.width, size.height));
	}
	// and an odd-sized frame (a crop) at full size is cut to even sides
	const cv::Size odd = WebRTCSink::EncodedSizeFor(cv::Size(641, 481), 1);
	CHECK(odd == cv::Size(640, 480));
}
#endif // LUMEN_WITH_WEBRTC

// MjpegSink shrinks a frame with cv::resize(Size(), 1/N, 1/N): for a frame narrower or shorter than N/2 pixels the computed size rounds to 0 and
// cv::resize throws (!dsize.empty()). The server accepts any crop of positive size (a 1..3 px wide strip, e.g. a mis-drawn ROI) and divisors
// up to 16 (the UI offers 8). On the sink's thread the throw is the known X1 terminate; called directly here so it is an assertion.
TEST_CASE("E: an MjpegSink at 1/8 size previews a 3 px wide crop without throwing", "[edge][outputs]") {
	auto logger = std::make_shared<Logger>("LumenCoreTests-edge-outputs-mjpeg.log");
	auto sink = std::make_shared<MjpegSink>(logger, "edge-outputs-mjpeg", 80);
	sink->SetSettings(80, 8);
	auto process = get(EdgeOutputsMjpegProcessTag{});
	cv::Mat strip(240, 3, CV_8UC3, cv::Scalar(10, 200, 30));
	CHECK_NOTHROW((sink.get()->*process)(std::vector<SourceResult>{ EdgeOutputsFrameResult(strip, 1) }));
}
