#pragma once

#include "ISink.h"
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

private:
	void Process(const std::vector<SourceResult>& results) override;

	int m_JpegQuality;
	// guards m_LatestJpegBase64: written by this sink's processing thread (Process()), read by the HTTP request thread
	// (via Manager::GetMjpegFrameBase64).
	mutable std::mutex m_Mutex;
	std::string m_LatestJpegBase64;
};
