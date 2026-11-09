#ifdef LUMEN_WITH_RECORD
#include "RecordSink.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <algorithm>
#include <cstdio>

namespace {
	// ticks per second of the encoder's pts (a multiple of every common frame rate, and what MPEG uses)
	constexpr int kPtsTimeBaseDen = 90000;
	// a keyframe at least this often (seconds), so seeking stays quick and a power cut loses little, at any frame rate
	constexpr int kKeyframeIntervalSeconds = 2;

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

	// A new sink on a folder that already holds segments (a restarted recording, the service restarting) numbers its segments after the highest
	// one there, so it never overwrites an earlier session's footage and the filename order stays chronological.
	for (const auto& entry : std::filesystem::directory_iterator(config.dstFolder, ec)) {
		const std::string stem = entry.path().stem().string();
		if (stem.rfind("segment_", 0) != 0 || stem.size() <= 8) continue;
		const std::string digits = stem.substr(8);
		if (digits.find_first_not_of("0123456789") != std::string::npos || digits.size() > 9) continue;
		m_SegmentIndex = std::max(m_SegmentIndex, std::stoi(digits) + 1);
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

	// Never evict the segment being written. It is found by its name: it is the last of the sort only when nothing in the folder is newer than it
	// (a folder that still holds the files of an earlier session, with higher numbers, used to have the open segment deleted from under the muxer).
	std::vector<SegmentInfo> evictable;
	for (const SegmentInfo& segment : segments)
		if (segment.filename != m_CurrentSegmentFile) evictable.push_back(segment);
	const size_t evictableCount = evictable.size();
	size_t remaining = segments.size();

	size_t index = 0;
	while (index < evictableCount &&
		((m_Config.maxFileCount > 0 && static_cast<int>(remaining) > m_Config.maxFileCount) ||
		 (m_Config.maxFolderSizeBytes > 0 && totalSize > static_cast<uintmax_t>(m_Config.maxFolderSizeBytes)))) {
		const SegmentInfo& oldest = evictable[index];
		std::filesystem::path videoPath = std::filesystem::path(m_Config.dstFolder) / oldest.filename;
		std::filesystem::remove(videoPath, ec);
		std::filesystem::path sidecarPath = videoPath;
		sidecarPath.replace_extension(".jsonl");
		std::filesystem::remove(sidecarPath, ec);
		if (m_Logger) m_Logger->EnterLog("RecordSink: retention deleted " + oldest.filename);
		totalSize -= oldest.size;
		remaining--;
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
	// a fine, fixed time base: frames carry their real capture times, so the file plays at the rate it was captured at (variable frame rate)
	m_CodecContext->time_base = AVRational{ 1, kPtsTimeBaseDen };
	const int encoderFps = std::max(1, static_cast<int>(std::lround(EncoderFps())));
	m_CodecContext->framerate = AVRational{ encoderFps, 1 };
	m_CodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
	m_CodecContext->bit_rate = static_cast<int64_t>(m_Config.bitrateKbps) * 1000;
	// the keyframes come from time (EncodeAndWrite forces one every kKeyframeIntervalSeconds); this is only the ceiling for a camera faster than the configured fps
	m_CodecContext->gop_size = std::max(encoderFps * kKeyframeIntervalSeconds, 240);
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
	// With an unpatched ffmpeg-rockchip, freeing an h264_rkmpp encoder makes libmpp log "mpp_mem_pool_put invalid mem pool ptr ... caller
	// mpp_frame_deinit": the encoder frees its end-of-stream frame twice. Harmless (the pool refuses the second put), and fixed in the FFmpeg that
	// scripts/install-deps.sh builds (scripts/patches/ffmpeg-rockchip-rkmppenc-eos.patch); the test "closing an h264_rkmpp recording does not make
	// libmpp complain" checks it. Nothing here can avoid it: draining without a flush, extra receives and a different close order were all tried.
	if (m_YuvFrame) av_frame_free(&m_YuvFrame);
	if (m_SwsContext) { sws_freeContext(m_SwsContext); m_SwsContext = nullptr; }
	if (m_CodecContext) avcodec_free_context(&m_CodecContext);
	m_EncoderWidth = m_EncoderHeight = 0;
}

void RecordSink::StartNewSegment(int width, int height)
{
	CloseCurrentSegment();

	if (!EnsureEncoderInitialized(width, height)) return;
	m_LastPts = -1;
	m_FirstCaptureUs = 0;
	m_NextKeyPts = 0;
	m_RotateFromPts = AV_NOPTS_VALUE;
	OpenSegmentFiles();
}

bool RecordSink::OpenSegmentFiles()
{
	std::string baseName = SegmentBaseName(m_SegmentIndex++);
	m_CurrentSegmentFile = baseName + ".mp4";
	std::filesystem::path videoPath = std::filesystem::path(m_Config.dstFolder) / (baseName + ".mp4");
	std::filesystem::path sidecarPath = std::filesystem::path(m_Config.dstFolder) / (baseName + ".jsonl");

	if (avformat_alloc_output_context2(&m_FormatContext, nullptr, "mp4", videoPath.string().c_str()) < 0 || !m_FormatContext) {
		if (m_Logger) m_Logger->EnterLog(LogLevel::Error, "RecordSink: avformat_alloc_output_context2 failed for " + videoPath.string());
		m_FormatContext = nullptr;
		return false;
	}

	m_VideoStream = avformat_new_stream(m_FormatContext, nullptr);
	avcodec_parameters_from_context(m_VideoStream->codecpar, m_CodecContext);
	m_VideoStream->time_base = m_CodecContext->time_base;
	m_VideoStream->avg_frame_rate = m_CodecContext->framerate;

	if (avio_open(&m_FormatContext->pb, videoPath.string().c_str(), AVIO_FLAG_WRITE) < 0) {
		if (m_Logger) m_Logger->EnterLog(LogLevel::Error, "RecordSink: avio_open failed for " + videoPath.string());
		avformat_free_context(m_FormatContext);
		m_FormatContext = nullptr;
		m_VideoStream = nullptr;
		return false;
	}

	// Fragmented MP4: a header up front and a self-contained fragment at every keyframe, so a power cut (the end of every match) loses
	// at most one GOP instead of a segment whose index was never written.
	AVDictionary* muxerOptions = nullptr;
	av_dict_set(&muxerOptions, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
	const int headerResult = avformat_write_header(m_FormatContext, &muxerOptions);
	av_dict_free(&muxerOptions);
	if (headerResult < 0) {
		if (m_Logger) m_Logger->EnterLog(LogLevel::Error, "RecordSink: avformat_write_header failed for " + videoPath.string());
		avio_closep(&m_FormatContext->pb);
		avformat_free_context(m_FormatContext);
		m_FormatContext = nullptr;
		m_VideoStream = nullptr;
		return false;
	}

	m_TelemetrySidecar.open(sidecarPath, std::ios::out | std::ios::trunc);
	m_SegmentPtsBase = AV_NOPTS_VALUE;
	m_SegmentStartedAt = std::chrono::steady_clock::now();
	m_SidecarFlushedAt = m_SegmentStartedAt;
	if (m_Logger) m_Logger->EnterLog("RecordSink: started segment " + videoPath.string());

	EnforceRetention();
	return true;
}

void RecordSink::WritePacket(AVPacket* packet)
{
	// each file is a standalone MP4 whose timestamps start at 0, while the encoder's run on across files
	if (m_SegmentPtsBase == AV_NOPTS_VALUE) m_SegmentPtsBase = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
	if (packet->pts != AV_NOPTS_VALUE) packet->pts -= m_SegmentPtsBase;
	if (packet->dts != AV_NOPTS_VALUE) packet->dts -= m_SegmentPtsBase;
	packet->stream_index = m_VideoStream->index;
	av_packet_rescale_ts(packet, m_CodecContext->time_base, m_VideoStream->time_base);
	av_interleaved_write_frame(m_FormatContext, packet);
}

void RecordSink::CloseSegmentFiles()
{
	if (m_FormatContext) {
		av_write_trailer(m_FormatContext);
		avio_closep(&m_FormatContext->pb);
		avformat_free_context(m_FormatContext);
		m_FormatContext = nullptr;
		m_VideoStream = nullptr;
	}
	if (m_TelemetrySidecar.is_open()) {
		m_TelemetrySidecar.close();
	}
}

void RecordSink::CloseCurrentSegment()
{
	if (m_FormatContext && m_CodecContext) {
		// flush the encoder before finalising the container (send a null frame, drain every remaining packet):
		// libx264 holds frames in its lookahead even with max_b_frames=0
		avcodec_send_frame(m_CodecContext, nullptr);
		AVPacket* packet = av_packet_alloc();
		while (avcodec_receive_packet(m_CodecContext, packet) == 0) {
			WritePacket(packet);
			av_packet_unref(packet);
		}
		av_packet_free(&packet);
	}
	CloseSegmentFiles();
	// a drained encoder cannot take frames again, so it goes too; this is the end of the recording or a change of frame size
	ShutdownEncoder();
	m_RotateFromPts = AV_NOPTS_VALUE;
}

int64_t RecordSink::NextPts(const SourceResult& result)
{
	// a source that stamps no capture time (0) falls back to the time the frame reaches the sink
	const uint64_t captureUs = result.captureTimeUs != 0 ? result.captureTimeUs : SourceResult::NowUs();
	if (m_FirstCaptureUs == 0) m_FirstCaptureUs = captureUs;
	int64_t pts = captureUs >= m_FirstCaptureUs
		? av_rescale_q(static_cast<int64_t>(captureUs - m_FirstCaptureUs), AVRational{ 1, 1000000 }, AVRational{ 1, kPtsTimeBaseDen })
		: 0;
	// strictly increasing, as the encoder and the muxer need (a repeated or earlier capture time still gets its own tick)
	if (pts <= m_LastPts) pts = m_LastPts + 1;
	m_LastPts = pts;
	return pts;
}

void RecordSink::EncodeAndWrite(const Frame& frame, const SourceResult& result)
{
	StageLap lap(SinkProfile());
	// a camera's hardware-decoded NV12 picture is converted straight from its planes; anything else goes through BGR
	const bool nv12 = frame.IsNv12();
	// even sides, as YUV 4:2:0 needs (libx264 refuses an odd one): an odd BGR frame loses its last column or row
	const cv::Size size = nv12 ? cv::Size(frame.size().width & ~1, frame.size().height & ~1)
		: cv::Size(frame.AsBgr().cols & ~1, frame.AsBgr().rows & ~1);
	if (size.width < 2 || size.height < 2) return;
	// a new encoder only for the first segment, after a failure, or when the frame size changes
	if (!m_FormatContext || !m_CodecContext || m_EncoderWidth != size.width || m_EncoderHeight != size.height) {
		// after a failed start for this size, the next try waits a few seconds instead of opening (and logging the failure of) an encoder per frame
		const auto now = std::chrono::steady_clock::now();
		if (m_StartFailedSize == size && now - m_StartFailedAt < std::chrono::seconds(5)) return;
		StartNewSegment(size.width, size.height);
		lap.Mark("segment-start");
		if (!m_FormatContext || !m_CodecContext) {
			m_StartFailedSize = size;
			m_StartFailedAt = now;
			return;
		}
		m_StartFailedSize = cv::Size();
	}
	if (!m_FormatContext || !m_CodecContext) return;
	const int64_t pts = NextPts(result);
	// a due segment asks for a keyframe now; the file changes when that keyframe comes out of the encoder (below), so every file starts on one
	bool forceKeyframe = false;
	if (m_RotateFromPts == AV_NOPTS_VALUE &&
		std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_SegmentStartedAt).count() >= m_Config.segmentSeconds) {
		m_RotateFromPts = pts; // this frame's pts
		forceKeyframe = true;
	}
	if (pts >= m_NextKeyPts) forceKeyframe = true;
	if (forceKeyframe) m_NextKeyPts = pts + static_cast<int64_t>(kKeyframeIntervalSeconds) * kPtsTimeBaseDen;

	if (nv12) {
		if (!m_Nv12Converter.Convert(frame, m_YuvFrame)) return;
	} else {
		const cv::Mat& bgrFrame = frame.AsBgr();
		const uint8_t* srcSlices[1] = { bgrFrame.data };
		int srcStride[1] = { static_cast<int>(bgrFrame.step) };
		sws_scale(m_SwsContext, srcSlices, srcStride, 0, m_EncoderHeight, m_YuvFrame->data, m_YuvFrame->linesize);
	}
	m_YuvFrame->pts = pts;
	m_YuvFrame->pict_type = forceKeyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
	lap.Mark("convert");

	if (avcodec_send_frame(m_CodecContext, m_YuvFrame) < 0) return;

	AVPacket* packet = av_packet_alloc();
	while (avcodec_receive_packet(m_CodecContext, packet) == 0) {
		lap.Mark("encode");
		// the keyframe asked for above (or the GOP's own, if it comes first) starts the next file; the packets before it finish this one
		if (m_RotateFromPts != AV_NOPTS_VALUE && (packet->flags & AV_PKT_FLAG_KEY) && packet->pts >= m_RotateFromPts) {
			CloseSegmentFiles();
			m_RotateFromPts = AV_NOPTS_VALUE;
			if (!OpenSegmentFiles()) {
				// the next frame starts over with a new encoder and file
				av_packet_unref(packet);
				break;
			}
			lap.Mark("segment-start");
		}
		WritePacket(packet);
		av_packet_unref(packet);
		lap.Mark("mux");
	}
	av_packet_free(&packet);
	if (!m_FormatContext) return;

	if (m_TelemetrySidecar.is_open()) {
		nlohmann::json record{
			{"frameNumber", result.frameNumber},
			{"captureTimeUs", result.captureTimeUs},
			{"producedTimeUs", result.producedTimeUs},
			{"json", result.json.has_value() ? result.json.value() : nlohmann::json(nullptr)},
		};
		// flushed about once a second (and on close) so the sidecar can still be tailed while recording without a write per frame
		// (a class name that is not UTF-8 would make dump() throw)
		m_TelemetrySidecar << record.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
		const auto now = std::chrono::steady_clock::now();
		if (now - m_SidecarFlushedAt >= std::chrono::seconds(1)) {
			m_TelemetrySidecar.flush();
			m_SidecarFlushedAt = now;
		}
		lap.Mark("sidecar");
	}
}

double RecordSink::EncoderFps() const
{
	return m_MeasuredFps > 0.0 ? m_MeasuredFps : static_cast<double>(std::max(1, m_Config.fps));
}

void RecordSink::AddToPreroll(const SourceResult& result)
{
	PrerollFrame entry;
	entry.bgr = result.frame->AsBgr().clone();
	entry.result = result;
	entry.result.frame.reset();
	m_Preroll.push_back(std::move(entry));

	// a leading frame from before this sink started: its capture time is far behind the next one's
	while (m_Preroll.size() >= 2 && m_Preroll[1].result.captureTimeUs > m_Preroll[0].result.captureTimeUs + 1'000'000ull)
		m_Preroll.erase(m_Preroll.begin());

	// enough to measure: eight frames, or two and half a second of them
	const size_t count = m_Preroll.size();
	const uint64_t spanUs = m_Preroll.back().result.captureTimeUs - m_Preroll.front().result.captureTimeUs;
	if (count >= 8 || (count >= 2 && spanUs >= 500'000ull)) FlushPreroll(false);
}

void RecordSink::FlushPreroll(bool atStop)
{
	if (m_Preroll.empty()) return;
	// a rate from the capture times of the frames waiting; fewer than two frames (a still image) or no time between them keep the configured one
	if (m_Preroll.size() >= 2 && m_Preroll.back().result.captureTimeUs > m_Preroll.front().result.captureTimeUs) {
		const double spanSeconds = static_cast<double>(m_Preroll.back().result.captureTimeUs - m_Preroll.front().result.captureTimeUs) / 1e6;
		m_MeasuredFps = std::clamp(static_cast<double>(m_Preroll.size() - 1) / spanSeconds, 1.0, 240.0);
		if (m_Logger) m_Logger->EnterLog("RecordSink: the source delivers " + std::to_string(m_MeasuredFps) + " frames per second (configured " + std::to_string(m_Config.fps) + ")");
	} else {
		m_MeasuredFps = static_cast<double>(std::max(1, m_Config.fps));
	}
	(void)atStop;
	std::vector<PrerollFrame> waiting;
	waiting.swap(m_Preroll);
	for (PrerollFrame& entry : waiting) EncodeAndWrite(Frame(entry.bgr), entry.result);
}

void RecordSink::Process(const std::vector<SourceResult>& results)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	for (const SourceResult& result : results) {
		if (result.frame.has_value() && !result.frame.value().empty()) {
			// until the source's rate is known the frames wait (see PrerollFrame); a later change of frame size reopens the encoder at the rate found
			if (m_MeasuredFps <= 0.0) AddToPreroll(result);
			else EncodeAndWrite(*result.frame, result);
		}
	}
}

void RecordSink::OnStopped()
{
	// ISink::Toggle(false) has already joined the processing thread, so Process() cannot be running concurrently
	std::lock_guard<std::mutex> lock(m_Mutex);
	FlushPreroll(true); // a recording that ended before eight frames came (or of one still picture) still gets what it had
	CloseCurrentSegment();
	m_MeasuredFps = 0.0; // the next recording measures its own source
}

#endif // LUMEN_WITH_RECORD
