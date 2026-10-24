#pragma once
#ifdef LUMEN_WITH_MPP_JPEG

#include <opencv2/opencv.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>

// Hardware MJPEG decode via the RK3588 JPEG-decode VPU (rockchip_mpp) for V4l2CameraBackend's MJPEG cameras (~2-3ms per 1080p
// frame vs ~12ms for cv::imdecode). Initialised lazily; every failure returns false rather than throwing, and the caller
// (V4l2CameraBackend::Grab()) falls back to software cv::imdecode.
class MppJpegDecoder
{
public:
	~MppJpegDecoder();

	// A decoded picture left in the decoder's own buffer: the two planes of an NV12 image, with strides that may exceed the width. `owner`
	// holds the buffer; the planes are valid for as long as a copy of it lives (it returns the buffer to the decoder's pool when the last copy goes).
	struct Nv12Planes {
		cv::Mat y;  // CV_8UC1, height x width
		cv::Mat uv; // CV_8UC2, height/2 x width/2
		std::shared_ptr<void> owner;
	};

	// Decodes one JPEG frame (raw compressed bytes, no copy) and hands back the decoder's NV12 output without copying or converting it: the
	// CPU cache lines for the buffer are invalidated once here, so the planes can be read straight away.
	// Returns false on ANY failure (init failure, chroma layout the decoder cannot give as 4:2:0, MPP decode error); the caller falls back
	// to a software decode. Not thread-safe: call only from the capture thread.
	bool DecodeNv12(const uint8_t* jpegData, size_t jpegSize, int width, int height, Nv12Planes& out);

	// DecodeNv12 followed by a copy into `dst`, which the CALLER must have sized/typed via FramePool::Acquire (CV_8UC1 for asGray, CV_8UC3
	// otherwise) and own the pool token for. Returns false, dst untouched, on ANY failure.
	bool Decode(const uint8_t* jpegData, size_t jpegSize, int width, int height, bool asGray, cv::Mat& dst);

	// where the last successful Decode() spent its time: in the decoder block, then copying/converting the frame out on the CPU
	double LastHwUs() const { return m_LastHwUs; }
	double LastCopyOutUs() const { return m_LastCopyOutUs; }

private:
	bool EnsureInitialized();
	// (Re)creates m_BufGroup, sized for at least bufSize: it backs the OUTPUT frame buffer this class pre-allocates for MPP
	// (advanced put_packet/get_frame interface). Not registered via MPP_DEC_SET_EXT_BUF_GROUP; only a real MppBuffer is needed.
	bool EnsureOutputBufferGroup(size_t bufSize);
	// (Re)creates m_InputBufGroup, sized for at least jpegSize: it backs INPUT packets, which must be real MppBuffers,
	// separate from m_BufGroup.
	bool EnsureInputBufferGroup(size_t jpegSize);

	void* m_Ctx = nullptr;   // MppCtx
	void* m_Api = nullptr;   // MppApi*
	bool m_InitAttempted = false;
	bool m_InitOk = false;
	double m_LastHwUs = 0;
	double m_LastCopyOutUs = 0;

	void* m_BufGroup = nullptr; // MppBufferGroup, owns the OUTPUT frame buffer MPP decodes into
	size_t m_BufGroupSize = 0;

	void* m_InputBufGroup = nullptr; // MppBufferGroup, owns the INPUT packet buffer
	size_t m_InputBufGroupSize = 0;
};

#endif // LUMEN_WITH_MPP_JPEG
