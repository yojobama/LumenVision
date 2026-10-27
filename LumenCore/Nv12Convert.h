#pragma once
#if defined(LUMEN_WITH_WEBRTC) || defined(LUMEN_WITH_RECORD)

#include "Frame.h"
#include <opencv2/opencv.hpp>

extern "C" {
#include <libavutil/frame.h>
}

// Fills an encoder's input frame from an NV12 Frame (a camera's hardware-decoded picture) without going through BGR: the planes are read where they are,
// shrunk to the destination's size if it differs, and written into its NV12 or YUV420P planes. The source is JPEG-range YUV and the encoders expect
// limited range, so each plane goes through a lookup table on the way (one pass over the pixels that was a copy anyway; NEON table lookups on ARM64).
class Nv12ToAvFrame {
public:
	// squeezeRange false keeps the JPEG range (the planes are copied as they are), for a consumer that takes full-range input such as a JPEG encoder.
	explicit Nv12ToAvFrame(bool squeezeRange = true) : m_SqueezeRange(squeezeRange) {}

	// `src` must be an NV12 Frame; `dst` is allocated (width, height, format NV12 or YUV420P, even dimensions). Returns false for anything else.
	bool Convert(const Frame& src, AVFrame* dst);

private:
	bool m_SqueezeRange;
	cv::Mat m_ScaledY, m_ScaledUv, m_Chroma; // reused between frames
};

#endif
