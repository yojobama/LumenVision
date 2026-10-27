#pragma once

#include "Frame.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// JPEG encoding on the RK3588's JPEG encoder cores (FFmpeg's mjpeg_rkmpp), for the MJPEG previews. A camera's NV12 frame goes to the encoder from
// its planes as it is (JPEG range, no BGR conversion), shrunk first if the preview is scaled down; a BGR frame (a detector's annotated output) is
// expanded to 32-bit BGR straight into the encoder's input, which the RK3588's encoder converts itself. Encode() returns false wherever there is no
// such encoder (x64 and Windows builds, FFmpeg without rkmpp) or it fails, and the caller encodes on the CPU instead. LUMEN_MJPEG_HW_DISABLE=1 turns
// it off. Not thread-safe: one per sink, used from its processing thread.
class MjpegHwEncoder {
public:
	MjpegHwEncoder();
	~MjpegHwEncoder();
	MjpegHwEncoder(const MjpegHwEncoder&) = delete;
	MjpegHwEncoder& operator=(const MjpegHwEncoder&) = delete;

	// quality is cv::IMWRITE_JPEG_QUALITY's 1-100 (the encoder's quality factor tops out at 99); scaleDivisor N shrinks to 1/N of each side
	bool Encode(const Frame& frame, int quality, int scaleDivisor, std::vector<uint8_t>& jpeg);

	// why the last Encode() returned false (empty when it has not failed)
	const std::string& LastError() const { return m_LastError; }

private:
	struct Impl;
	std::unique_ptr<Impl> m_Impl;
	std::string m_LastError;
};
