#pragma once

#include "ISink.h"
#include <atomic>
#include <mutex>
#include <string>

// A simple preview stream: one plain JPEG per frame, viewable by any browser <img> tag or curl, with no H.264 encode or
// signalling (unlike WebRTCSink). Terminal sink bound to a single frame-producing node. Always available (no LUMEN_WITH_* guard).
class MjpegSink : public ISink
{
public:
	// jpegQuality is cv::IMWRITE_JPEG_QUALITY's own 0-100 scale.
	MjpegSink(std::shared_ptr<Logger> logger, std::string id, int jpegQuality = 80);

	// The most recently encoded frame, base64-encoded because raw JPEG bytes (with embedded nulls) would be truncated or
	// corrupted by SWIG's null-terminated std::string typemap. Empty until the first frame has been processed.
	std::string GetLatestJpegBase64() const;

	// Takes effect on the next frame. scaleDivisor N shrinks each frame to 1/N of its width and height before encoding (1 = full size).
	void SetSettings(int jpegQuality, int scaleDivisor);

private:
	void Process(const std::vector<SourceResult>& results) override;

	std::atomic<int> m_JpegQuality;
	std::atomic<int> m_ScaleDivisor{ 1 };
	// guards m_LatestJpegBase64: written by this sink's processing thread (Process()), read by the HTTP request thread
	// (via Manager::GetMjpegFrameBase64).
	mutable std::mutex m_Mutex;
	std::string m_LatestJpegBase64;
};
