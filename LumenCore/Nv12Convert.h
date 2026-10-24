#pragma once
#if defined(LUMEN_WITH_WEBRTC) || defined(LUMEN_WITH_RECORD)

#include "Frame.h"

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

// Fills an encoder's input frame from an NV12 Frame (a camera's hardware-decoded picture) without going through BGR: libswscale
// reads the two planes where they are, rescales to the destination's size if it differs and converts to its pixel format (NV12 or
// YUV420P). The source is JPEG-range YUV and the encoders expect limited range, so the range is converted on the way.
class Nv12ToAvFrame {
public:
	Nv12ToAvFrame() = default;
	~Nv12ToAvFrame();
	Nv12ToAvFrame(const Nv12ToAvFrame&) = delete;
	Nv12ToAvFrame& operator=(const Nv12ToAvFrame&) = delete;

	// `src` must be an NV12 Frame; `dst` is allocated (width, height, format NV12 or YUV420P). Returns false when swscale cannot do it.
	bool Convert(const Frame& src, AVFrame* dst);

private:
	struct Key {
		int srcW = 0, srcH = 0, dstW = 0, dstH = 0, format = -1;
		bool operator==(const Key& other) const { return srcW == other.srcW && srcH == other.srcH && dstW == other.dstW && dstH == other.dstH && format == other.format; }
	};
	SwsContext* m_Sws = nullptr;
	Key m_Key;
};

#endif
