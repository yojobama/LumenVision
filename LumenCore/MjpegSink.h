#pragma once

#include "ISink.h"
#include "MjpegHwEncoder.h"
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

	// The most recently encoded frame, base64-encoded (SWIG's std::string typemap would cut raw JPEG bytes at the first null). Empty until the
	// first frame has been processed. Encoded on each call: the server uses CopyLatestJpeg instead.
	std::string GetLatestJpegBase64() const;

	// Copies the most recent JPEG into `buffer` when it fits in `capacity` bytes and returns its size either way, so a caller whose buffer is too
	// small grows it and calls again; 0 before the first frame. Like GetLatestJpegBase64 it counts as a viewer's request. The server reads frames
	// this way into a pooled buffer: as a base64 string (twice over, once in UTF-16) every frame was a large-object allocation in .NET, and with four
	// tiles that was 58 MB a second, 13 full collections a second and 15% of the time paused.
	int CopyLatestJpeg(void* buffer, int capacity) const;
	// Increments with every new JPEG (0 before the first), so a streaming caller can tell a new frame from the one it already sent.
	uint64_t GetLatestJpegSequence() const;

	// A preview is only wanted while something asks for its frames (the server's stream and frame endpoints poll about every 100 ms):
	// encoding stops about a second after the last request, and the camera and detectors can drop back to their cheap paths.
	bool IsConsumingFrames() const override;
	// watched, and long enough since the last encode that Process() would take a frame now (it takes one per 80 ms at most)
	bool WantsFrameNow() const override;

	// Takes effect on the next frame. scaleDivisor N shrinks each frame to 1/N of its width and height before encoding (1 = full size).
	void SetSettings(int jpegQuality, int scaleDivisor);

private:
	void Process(const std::vector<SourceResult>& results) override;

	std::atomic<int> m_JpegQuality;
	std::atomic<int> m_ScaleDivisor{ 1 };
	// when GetLatestJpegBase64 last ran and when Process last encoded (SourceResult::NowUs() epoch); the first is primed at construction so a
	// new sink encodes until its first viewer arrives
	mutable std::atomic<uint64_t> m_LastRequestUs{ 0 };
	std::atomic<uint64_t> m_LastEncodeUs{ 0 };
	// guards m_LatestJpeg and m_Sequence: written by this sink's processing thread (Process()), read by the HTTP request threads (via Manager).
	// The JPEG is shared, so a reader copies it outside the lock while Process() moves on to the next one.
	mutable std::mutex m_Mutex;
	std::shared_ptr<const std::vector<uint8_t>> m_LatestJpeg;
	uint64_t m_Sequence = 0;

	// the RK3588's JPEG encoder when there is one (the CPU encoder otherwise, or when it fails); used only by Process()
	MjpegHwEncoder m_HwEncoder;
	std::shared_ptr<Logger> m_Logger;
	bool m_EncoderLogged = false;
};
