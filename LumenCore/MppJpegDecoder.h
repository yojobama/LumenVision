#pragma once
#ifdef LUMEN_WITH_MPP_JPEG

#include <opencv2/opencv.hpp>
#include <cstddef>
#include <cstdint>

// Hardware MJPEG decode via the RK3588 JPEG-decode VPU (rockchip_mpp) for V4l2CameraBackend's MJPEG cameras (~2-3ms per 1080p
// frame vs ~12ms for cv::imdecode). Initialised lazily; every failure returns false rather than throwing, and the caller
// (V4l2CameraBackend::Grab()) falls back to software cv::imdecode.
class MppJpegDecoder
{
public:
	~MppJpegDecoder();

	// Decodes one JPEG frame (raw compressed bytes, no copy) into `dst`, which the CALLER must have sized/typed via
	// FramePool::Acquire (CV_8UC1 for asGray, CV_8UC3 otherwise) and own the pool token for; this class only writes into it.
	// Returns false, dst untouched, on ANY failure (init failure, chroma layout other than 4:2:0, MPP decode error).
	// Not thread-safe: call only from the capture thread.
	bool Decode(const uint8_t* jpegData, size_t jpegSize, int width, int height, bool asGray, cv::Mat& dst);

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

	void* m_BufGroup = nullptr; // MppBufferGroup, owns the OUTPUT frame buffer MPP decodes into
	size_t m_BufGroupSize = 0;

	void* m_InputBufGroup = nullptr; // MppBufferGroup, owns the INPUT packet buffer
	size_t m_InputBufGroupSize = 0;
};

#endif // LUMEN_WITH_MPP_JPEG
