#pragma once
#ifdef LUMEN_WITH_MPP_JPEG

#include <opencv2/opencv.hpp>
#include <cstddef>
#include <cstdint>

// Hardware MJPEG decode via the RK3588's dedicated JPEG-decode VPU (rockchip_mpp), replacing
// libjpeg-turbo's software cv::imdecode for V4l2CameraBackend's MJPEG cameras - the RK3588's own
// measurements put a real hardware decode at ~2-3ms/1080p-frame vs ~12ms software (see
// docs/PERFORMANCE_ANALYSIS.md's own §1). Lazily initialized on first use (not every board this
// project targets has RK3588's JPEG decode block active/available), and every failure - init, an
// output chroma layout this class doesn't handle, a bad frame - returns false rather than
// throwing, exactly like RgaColorConverter's own contract: the caller (V4l2CameraBackend::Grab())
// falls back to its existing software cv::imdecode path, so a board with no working JPEG VPU (or
// a camera stream this decoder can't handle) keeps working exactly as before.
class MppJpegDecoder
{
public:
	~MppJpegDecoder();

	// Decodes one JPEG frame (raw compressed bytes, exactly as V4L2 handed them over - no copy)
	// into `dst`, which the CALLER must already have sized/typed via FramePool::Acquire (CV_8UC1
	// for asGray, CV_8UC3 otherwise) and own the pool token for - this class only ever writes
	// into it (copyTo/cvtColor), it never allocates or touches FramePool itself, so the caller's
	// existing pool-owner (CameraGrabResult::poolOwner) stays correct without this class needing
	// to know anything about it. Returns false - dst left untouched - on ANY failure: lazy MPP
	// init failure (no JPEG VPU on this SoC/board, device busy, driver missing), a decoded chroma
	// layout other than 4:2:0 (the only one this class handles - see the .cpp), or a decode error
	// MPP itself reports. Not thread-safe - V4l2CameraBackend calls this only from its own
	// capture thread, same as every other decode path in Grab().
	bool Decode(const uint8_t* jpegData, size_t jpegSize, int width, int height, bool asGray, cv::Mat& dst);

private:
	bool EnsureInitialized();
	// (Re)creates m_BufGroup sized for bufSize and registers it with the decoder via
	// MPP_DEC_SET_EXT_BUF_GROUP - see the .cpp's own comment on why this is mandatory, not
	// optional, even for a single-frame codec with no reference chaining.
	bool SetupBufferGroup(size_t bufSize);
	// (Re)creates m_InputBufGroup, sized for at least jpegSize - the group backing INPUT packets
	// (the compressed bytes handed to the decoder), separate from m_BufGroup (the decoder's own
	// OUTPUT frame buffers) - see the .cpp's own comment on why the input packet also needs to be
	// a real MppBuffer, not a plain heap pointer.
	bool EnsureInputBufferGroup(size_t jpegSize);

	void* m_Ctx = nullptr;   // MppCtx
	void* m_Api = nullptr;   // MppApi*
	bool m_InitAttempted = false;
	bool m_InitOk = false;

	void* m_BufGroup = nullptr; // MppBufferGroup, owns the frame buffers MPP decodes into
	size_t m_BufGroupSize = 0;

	void* m_InputBufGroup = nullptr; // MppBufferGroup, owns the INPUT packet buffer
	size_t m_InputBufGroupSize = 0;
};

#endif // LUMEN_WITH_MPP_JPEG
