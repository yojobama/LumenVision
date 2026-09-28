#pragma once
#ifdef LUMEN_WITH_RECORD

#include "ISink.h"
#include <opencv2/opencv.hpp>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

struct RecordSinkConfig {
	// relative to the server's working directory (like ImageFileSource/VideoFileSource's "images/"/"videos/");
	// created if missing
	std::string dstFolder;
	// "libx264" by default (same knob as WebRTCSinkConfig, e.g. "h264_rkmpp" on the Orange Pi);
	// see cmake/LumenFFmpeg.cmake for the GPL licensing implications
	std::string encoderName = "libx264";
	// higher than WebRTCSink's 4000 default: footage quality matters more than live latency
	int bitrateKbps = 8000;
	int fps = 30;
	// a new segment starts once the current one has been open this long, bounding what a crash/power loss can lose
	int segmentSeconds = 300;
	// 0 = unbounded (default); once set, EnforceRetention() deletes the oldest segment (by filename) first
	int64_t maxFolderSizeBytes = 0;
	int maxFileCount = 0;
};

// Terminal sink: binds to any single frame-producing node (raw camera or a detector's annotated output) and writes
// segmented MP4 files plus a JSON-Lines telemetry sidecar per segment (frameNumber/captureTimeUs/producedTimeUs/json per frame).
//
// Unlike WebRTCSink, this muxes into a container via libavformat (avformat_alloc_output_context2/avio_open/
// av_write_trailer); encoder setup mirrors WebRTCSink's (EnsureEncoderInitialized/ShutdownEncoder).
class RecordSink : public ISink {
public:
	RecordSink(std::shared_ptr<Logger> logger, std::string id, RecordSinkConfig config);
	~RecordSink() override;

	// filenames only (not full paths), newest first; resolved against this sink's dstFolder
	std::vector<std::string> ListSegments() const;
	// true if filename existed and was removed; false if it resolves outside dstFolder once normalised (path traversal)
	bool DeleteSegment(const std::string& filename);

private:
	void Process(const std::vector<SourceResult>& results) override;
	// ISink::Toggle(false), after the processing thread has stopped: finalises any open segment (trailer written, sidecar closed)
	void OnStopped() override;

	void StartNewSegment(int width, int height); // caller must already hold m_Mutex
	void CloseCurrentSegment();                  // caller must already hold m_Mutex
	void EnforceRetention();                     // caller must already hold m_Mutex

	bool EnsureEncoderInitialized(int width, int height); // caller must already hold m_Mutex
	void ShutdownEncoder();                                // caller must already hold m_Mutex
	void EncodeAndWrite(const cv::Mat& bgrFrame, const SourceResult& result); // caller must already hold m_Mutex

	std::shared_ptr<Logger> m_Logger;
	RecordSinkConfig m_Config;

	// guards every member below; Process() runs on ISink's background thread while ListSegments/DeleteSegment
	// can run on an HTTP request thread
	mutable std::mutex m_Mutex;

	AVFormatContext* m_FormatContext = nullptr;
	AVStream* m_VideoStream = nullptr;
	AVCodecContext* m_CodecContext = nullptr;
	SwsContext* m_SwsContext = nullptr;
	AVFrame* m_YuvFrame = nullptr;
	int m_EncoderWidth = 0;
	int m_EncoderHeight = 0;
	int64_t m_FrameCounter = 0; // resets to 0 at the start of each segment - AVStream pts is per-file

	std::ofstream m_TelemetrySidecar;
	std::chrono::steady_clock::time_point m_SegmentStartedAt;
	int m_SegmentIndex = 0; // monotonically increasing across this sink's whole lifetime, never reused
};

#endif // LUMEN_WITH_RECORD
