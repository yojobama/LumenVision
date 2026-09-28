#ifdef LUMEN_WITH_RECORD
#include "RecordSink.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <algorithm>
#include <cstdio>

namespace {
	// zero-padded so a filename sort is chronological; ListSegments()/EnforceRetention() rely on it
	std::string SegmentBaseName(int index)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "segment_%06d", index);
		return buf;
	}

	// basic sanitisation for a filename reaching std::filesystem::remove(); this is the function that deletes, so it checks too
	bool IsSafeSegmentFilename(const std::string& filename)
	{
		if (filename.empty()) return false;
		if (filename.find('/') != std::string::npos) return false;
		if (filename.find('\\') != std::string::npos) return false;
		if (filename.find("..") != std::string::npos) return false;
		return true;
	}
}

RecordSink::RecordSink(std::shared_ptr<Logger> logger, std::string id, RecordSinkConfig config)
	: ISink(logger, 1 /* maxSources */, false /* requireJson */, true /* requireFrame */, id)
	, m_Logger(logger)
	, m_Config(config)
{
	if (m_Logger) m_Logger->EnterLog("RecordSink constructed, dstFolder=" + config.dstFolder + ", encoder=" + config.encoderName);
	std::error_code ec;
	std::filesystem::create_directories(config.dstFolder, ec);
	if (ec && m_Logger) {
		m_Logger->EnterLog(LogLevel::Error, "RecordSink: failed to create dstFolder " + config.dstFolder + ": " + ec.message());
	}
}

RecordSink::~RecordSink()
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	CloseCurrentSegment(); // also shuts the encoder down
}

std::vector<std::string> RecordSink::ListSegments() const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	std::vector<std::string> names;
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(m_Config.dstFolder, ec)) {
		if (!entry.is_regular_file()) continue;
		if (entry.path().extension() != ".mp4") continue;
		names.push_back(entry.path().filename().string());
	}
	// newest first: a descending sort of the zero-padded names
	std::sort(names.rbegin(), names.rend());
	return names;
}

bool RecordSink::DeleteSegment(const std::string& filename)
{
	if (!IsSafeSegmentFilename(filename)) {
		if (m_Logger) m_Logger->EnterLog(LogLevel::Warning, "RecordSink::DeleteSegment refused an unsafe filename: " + filename);
		return false;
	}
	std::lock_guard<std::mutex> lock(m_Mutex);
	std::filesystem::path videoPath = std::filesystem::path(m_Config.dstFolder) / filename;
	std::error_code ec;
	bool removed = std::filesystem::remove(videoPath, ec) && !ec;
	// best-effort: a missing or undeletable sidecar does not make DeleteSegment report failure
	std::filesystem::path sidecarPath = videoPath;
	sidecarPath.replace_extension(".jsonl");
	std::filesystem::remove(sidecarPath, ec);
	return removed;
}

void RecordSink::EnforceRetention()
{
	if (m_Config.maxFileCount <= 0 && m_Config.maxFolderSizeBytes <= 0) return;

	struct SegmentInfo { std::string filename; uintmax_t size; };
	std::vector<SegmentInfo> segments;
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(m_Config.dstFolder, ec)) {
		if (!entry.is_regular_file() || entry.path().extension() != ".mp4") continue;
		segments.push_back({ entry.path().filename().string(), entry.file_size(ec) });
	}
	// oldest first, so eviction removes the oldest segment first (reverse of ListSegments())
	std::sort(segments.begin(), segments.end(), [](const SegmentInfo& a, const SegmentInfo& b) { return a.filename < b.filename; });

	// totalSize covers every segment, including the current one, so the folder-size cap bounds real disk usage
	uintmax_t totalSize = 0;
	for (const auto& s : segments) totalSize += s.size;

	// never evict the segment being written: it is the last entry after the ascending sort
	size_t evictableCount = segments.empty() ? 0 : segments.size() - 1;

	size_t index = 0;
	while (index < evictableCount &&
		((m_Config.maxFileCount > 0 && static_cast<int>(segments.size() - index) > m_Config.maxFileCount) ||
		 (m_Config.maxFolderSizeBytes > 0 && totalSize > static_cast<uintmax_t>(m_Config.maxFolderSizeBytes)))) {
		const SegmentInfo& oldest = segments[index];
		std::filesystem::path videoPath = std::filesystem::path(m_Config.dstFolder) / oldest.filename;
		std::filesystem::remove(videoPath, ec);
		std::filesystem::path sidecarPath = videoPath;
		sidecarPath.replace_extension(".jsonl");
		std::filesystem::remove(sidecarPath, ec);
		if (m_Logger) m_Logger->EnterLog("RecordSink: retention deleted " + oldest.filename);
		totalSize -= oldest.size;
		index++;
	}
}

bool RecordSink::EnsureEncoderInitialized(int width, int height)
{
	if (m_CodecContext && m_EncoderWidth == width && m_EncoderHeight == height) {
		return true;
	}
	ShutdownEncoder();

	const AVCodec* codec = avcodec_find_encoder_by_name(m_Config.encoderName.c_str());
	if (!codec) {
		if (m_Logger) m_Logger->EnterLog(LogLevel::Error, "RecordSink: encoder not found: " + m_Config.encoderName);
		return false;
	}

	m_CodecContext = avcodec_alloc_context3(codec);
	m_CodecContext->width = width;
	m_CodecContext->height = height;
	m_CodecContext->time_base = AVRational{ 1, m_Config.fps };
	m_CodecContext->framerate = AVRational{ m_Config.fps, 1 };
	m_CodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
	m_CodecContext->bit_rate = static_cast<int64_t>(m_Config.bitrateKbps) * 1000;
	m_CodecContext->gop_size = m_Config.fps * 2;
	// no B-frames, so MP4 muxing needs no DTS/PTS reordering
	m_CodecContext->max_b_frames = 0;
	// MP4 wants SPS/PPS in extradata (the avcC box), not repeated before every keyframe
	m_CodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	// "medium" preset: compression matters more than latency for recorded footage
	av_opt_set(m_CodecContext->priv_data, "preset", "medium", 0);

	int openResult = avcodec_open2(m_CodecContext, codec, nullptr);
	if (openResult < 0) {
		// av_strerror rather than the bare negative code, so failures are diagnosable
		char errBuf[256] = {};
		av_strerror(openResult, errBuf, sizeof(errBuf));
		if (m_Logger) m_Logger->EnterLog(LogLevel::Error, "RecordSink: avcodec_open2 failed for " + m_Config.encoderName + ": " + errBuf + " (code " + std::to_string(openResult) + ")");
		avcodec_free_context(&m_CodecContext);
		return false;
	}

	m_SwsContext = sws_getContext(width, height, AV_PIX_FMT_BGR24, width, height, AV_PIX_FMT_YUV420P,
		SWS_BILINEAR, nullptr, nullptr, nullptr);

	m_YuvFrame = av_frame_alloc();
	m_YuvFrame->format = AV_PIX_FMT_YUV420P;
	m_YuvFrame->width = width;
	m_YuvFrame->height = height;
	av_frame_get_buffer(m_YuvFrame, 32);

	m_EncoderWidth = width;
	m_EncoderHeight = height;
	return true;
}

void RecordSink::ShutdownEncoder()
{
	if (m_YuvFrame) av_frame_free(&m_YuvFrame);
	if (m_SwsContext) { sws_freeContext(m_SwsContext); m_SwsContext = nullptr; }
	if (m_CodecContext) avcodec_free_context(&m_CodecContext);
	m_EncoderWidth = m_EncoderHeight = 0;
}

void RecordSink::StartNewSegment(int width, int height)
{
	CloseCurrentSegment();

	if (!EnsureEncoderInitialized(width, height)) return;

	std::string baseName = SegmentBaseName(m_SegmentIndex++);
	std::filesystem::path videoPath = std::filesystem::path(m_Config.dstFolder) / (baseName + ".mp4");
	std::filesystem::path sidecarPath = std::filesystem::path(m_Config.dstFolder) / (baseName + ".jsonl");

	if (avformat_alloc_output_context2(&m_FormatContext, nullptr, "mp4", videoPath.string().c_str()) < 0 || !m_FormatContext) {
		if (m_Logger) m_Logger->EnterLog(LogLevel::Error, "RecordSink: avformat_alloc_output_context2 failed for " + videoPath.string());
		return;
	}

	m_VideoStream = avformat_new_stream(m_FormatContext, nullptr);
	avcodec_parameters_from_context(m_VideoStream->codecpar, m_CodecContext);
	m_VideoStream->time_base = m_CodecContext->time_base;

	if (avio_open(&m_FormatContext->pb, videoPath.string().c_str(), AVIO_FLAG_WRITE) < 0) {
		if (m_Logger) m_Logger->EnterLog(LogLevel::Error, "RecordSink: avio_open failed for " + videoPath.string());
		avformat_free_context(m_FormatContext);
		m_FormatContext = nullptr;
		m_VideoStream = nullptr;
		return;
	}

	if (avformat_write_header(m_FormatContext, nullptr) < 0) {
		if (m_Logger) m_Logger->EnterLog(LogLevel::Error, "RecordSink: avformat_write_header failed for " + videoPath.string());
		avio_closep(&m_FormatContext->pb);
		avformat_free_context(m_FormatContext);
		m_FormatContext = nullptr;
		m_VideoStream = nullptr;
		return;
	}

	m_TelemetrySidecar.open(sidecarPath, std::ios::out | std::ios::trunc);
	m_FrameCounter = 0;
	m_SegmentStartedAt = std::chrono::steady_clock::now();
	if (m_Logger) m_Logger->EnterLog("RecordSink: started segment " + videoPath.string());

	EnforceRetention();
}

void RecordSink::CloseCurrentSegment()
{
	if (m_FormatContext) {
		// flush the encoder before finalising the container (send a null frame, drain every remaining packet):
		// libx264 holds frames in its lookahead even with max_b_frames=0
		avcodec_send_frame(m_CodecContext, nullptr);
		AVPacket* packet = av_packet_alloc();
		while (avcodec_receive_packet(m_CodecContext, packet) == 0) {
			packet->stream_index = m_VideoStream->index;
			av_packet_rescale_ts(packet, m_CodecContext->time_base, m_VideoStream->time_base);
			av_interleaved_write_frame(m_FormatContext, packet);
			av_packet_unref(packet);
		}
		av_packet_free(&packet);

		av_write_trailer(m_FormatContext);
		avio_closep(&m_FormatContext->pb);
		avformat_free_context(m_FormatContext);
		m_FormatContext = nullptr;
		m_VideoStream = nullptr;
	}
	if (m_TelemetrySidecar.is_open()) {
		m_TelemetrySidecar.close();
	}
	// Each segment is a standalone MP4 with PTS starting at 0, so the encoder is torn down here and
	// StartNewSegment creates a fresh one (a reused encoder would see PTS going backwards).
	ShutdownEncoder();
}

void RecordSink::EncodeAndWrite(const cv::Mat& bgrFrame, const SourceResult& result)
{
	bool segmentExpired = m_FormatContext &&
		std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_SegmentStartedAt).count() >= m_Config.segmentSeconds;
	if (!m_FormatContext || m_EncoderWidth != bgrFrame.cols || m_EncoderHeight != bgrFrame.rows || segmentExpired) {
		StartNewSegment(bgrFrame.cols, bgrFrame.rows);
	}
	if (!m_FormatContext || !m_CodecContext) return;

	const uint8_t* srcSlices[1] = { bgrFrame.data };
	int srcStride[1] = { static_cast<int>(bgrFrame.step) };
	sws_scale(m_SwsContext, srcSlices, srcStride, 0, bgrFrame.rows, m_YuvFrame->data, m_YuvFrame->linesize);
	m_YuvFrame->pts = m_FrameCounter++;

	if (avcodec_send_frame(m_CodecContext, m_YuvFrame) < 0) return;

	AVPacket* packet = av_packet_alloc();
	while (avcodec_receive_packet(m_CodecContext, packet) == 0) {
		packet->stream_index = m_VideoStream->index;
		av_packet_rescale_ts(packet, m_CodecContext->time_base, m_VideoStream->time_base);
		av_interleaved_write_frame(m_FormatContext, packet);
		av_packet_unref(packet);
	}
	av_packet_free(&packet);

	if (m_TelemetrySidecar.is_open()) {
		nlohmann::json record{
			{"frameNumber", result.frameNumber},
			{"captureTimeUs", result.captureTimeUs},
			{"producedTimeUs", result.producedTimeUs},
			{"json", result.json.has_value() ? result.json.value() : nlohmann::json(nullptr)},
		};
		// flushed every line so the sidecar can be tailed while recording
		m_TelemetrySidecar << record.dump() << "\n";
		m_TelemetrySidecar.flush();
	}
}

void RecordSink::Process(const std::vector<SourceResult>& results)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	for (const SourceResult& result : results) {
		if (result.frame.has_value() && !result.frame.value().empty()) {
			EncodeAndWrite(result.frame->AsBgr(), result);
		}
	}
}

void RecordSink::OnStopped()
{
	// ISink::Toggle(false) has already joined the processing thread, so Process() cannot be running concurrently
	std::lock_guard<std::mutex> lock(m_Mutex);
	CloseCurrentSegment();
}

#endif // LUMEN_WITH_RECORD
