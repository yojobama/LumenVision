#ifdef LUMEN_WITH_MPP_JPEG
#include "MppJpegDecoder.h"

#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_meta.h>
#include <rockchip/rk_vdec_cfg.h>
#include <chrono>
#include <cstring>

// Uses MPP's advanced decode interface (decode_put_packet()+decode_get_frame()): JPEG decode does not work through the simple
// decode() API. The OUTPUT frame buffer is pre-allocated here from the caller's width/height and attached to the packet's
// metadata before decoding, so there is no info-change negotiation.
namespace {
	// MPP_ALIGN as in rockchip-linux/mpp's osal/inc/mpp_common.h (not on the public include path); only used with a=16.
	constexpr RK_U32 MppAlign(RK_U32 x, RK_U32 a) { return (x + a - 1) & ~(a - 1); }

	// shared by EnsureOutputBufferGroup and EnsureInputBufferGroup: tried in mpp_buffer.h priority order (DMA_HEAP > DRM > ION),
	// falling back if the preferred allocator is unavailable.
	MppBufferGroup CreateInternalGroup(const char* tag, bool cpuReads)
	{
		// cacheable first for a buffer the CPU reads back (a plain DMA_HEAP group is the write-combined "system-uncached" heap,
		// so reading it is slow); callers wrap CPU reads in mpp_buffer_sync_ro_begin/end
		const MppBufferType kTypesInPriorityOrder[] = {
			static_cast<MppBufferType>(MPP_BUFFER_TYPE_DMA_HEAP | (cpuReads ? MPP_BUFFER_FLAGS_CACHABLE : 0)),
			MPP_BUFFER_TYPE_DMA_HEAP, MPP_BUFFER_TYPE_DRM, MPP_BUFFER_TYPE_ION
		};
		MppBufferGroup group = nullptr;
		for (MppBufferType type : kTypesInPriorityOrder) {
			if (mpp_buffer_group_get(&group, type, MPP_BUFFER_INTERNAL, tag, __func__) == MPP_OK && group) return group;
			group = nullptr;
		}
		return nullptr;
	}
}

MppJpegDecoder::~MppJpegDecoder()
{
	// context first, then the buffer groups: freeing a group first could free memory the decoder still touches
	if (m_Ctx) mpp_destroy(static_cast<MppCtx>(m_Ctx));
	m_OutGroupHolder.reset(); // puts the output group once the frames still in flight have gone too
	if (m_InputBufGroup) mpp_buffer_group_put(static_cast<MppBufferGroup>(m_InputBufGroup));
}

bool MppJpegDecoder::EnsureInputBufferGroup(size_t jpegSize)
{
	if (m_InputBufGroup && m_InputBufGroupSize >= jpegSize) return true;
	if (m_InputBufGroup) {
		mpp_buffer_group_put(static_cast<MppBufferGroup>(m_InputBufGroup));
		m_InputBufGroup = nullptr;
		m_InputBufGroupSize = 0;
	}
	MppBufferGroup group = CreateInternalGroup("lumen_mpp_jpeg_in", false);
	if (!group) return false;
	m_InputBufGroup = group;
	m_InputBufGroupSize = jpegSize;
	return true;
}

bool MppJpegDecoder::EnsureOutputBufferGroup(size_t bufSize)
{
	if (m_BufGroup && m_BufGroupSize >= bufSize) return true;
	if (m_BufGroup) {
		m_OutGroupHolder.reset();
		m_BufGroup = nullptr;
		m_BufGroupSize = 0;
	}
	MppBufferGroup group = CreateInternalGroup("lumen_mpp_jpeg_out", true);
	if (!group) return false;
	m_BufGroup = group;
	m_BufGroupSize = bufSize;
	m_OutGroupHolder = std::shared_ptr<void>(group, [](void* g) { mpp_buffer_group_put(static_cast<MppBufferGroup>(g)); });
	return true;
}

bool MppJpegDecoder::EnsureInitialized()
{
	if (m_InitAttempted) return m_InitOk;
	m_InitAttempted = true;

	MppCtx ctx = nullptr;
	MppApi* api = nullptr;
	if (mpp_create(&ctx, &api) != MPP_OK) return false;
	if (mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG) != MPP_OK) {
		mpp_destroy(ctx);
		return false;
	}

	// MppDecCfg must be fetched, configured and re-applied before any decode call. "base:split_parse"=0 suits the one-shot pattern
	// (one complete JPEG per call, no cross-call boundary scanning).
	MppDecCfg cfg = nullptr;
	if (mpp_dec_cfg_init(&cfg) != MPP_OK) {
		mpp_destroy(ctx);
		return false;
	}
	if (api->control(ctx, MPP_DEC_GET_CFG, cfg) != MPP_OK) {
		mpp_dec_cfg_deinit(cfg);
		mpp_destroy(ctx);
		return false;
	}
	mpp_dec_cfg_set_u32(cfg, "base:split_parse", 0);
	if (api->control(ctx, MPP_DEC_SET_CFG, cfg) != MPP_OK) {
		mpp_dec_cfg_deinit(cfg);
		mpp_destroy(ctx);
		return false;
	}
	mpp_dec_cfg_deinit(cfg);

	// 4:2:2 JPEGs are converted to NV12 by the decoder block instead of coming out as NV16 (which Decode() would reject and
	// send to the software decoder); a failure here only means that fallback stays in place
	MppFrameFormat outputFormat = MPP_FMT_YUV420SP;
	api->control(ctx, MPP_DEC_SET_OUTPUT_FORMAT, &outputFormat);

	m_Ctx = ctx;
	m_Api = api;
	m_InitOk = true;
	return true;
}

bool MppJpegDecoder::Decode(const uint8_t* jpegData, size_t jpegSize, int width, int height, bool asGray, cv::Mat& dst)
{
	Nv12Planes planes;
	if (!DecodeNv12(jpegData, jpegSize, width, height, planes)) return false;
	const auto copyStart = std::chrono::steady_clock::now();
	if (asGray) planes.y.copyTo(dst); // the Y plane is the greyscale image (no conversion)
	else cv::cvtColorTwoPlane(planes.y, planes.uv, dst, cv::COLOR_YUV2BGR_NV12);
	m_LastCopyOutUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - copyStart).count();
	return true;
}

bool MppJpegDecoder::DecodeNv12(const uint8_t* jpegData, size_t jpegSize, int width, int height, Nv12Planes& out)
{
	m_LastHwUs = m_LastCopyOutUs = 0;
	if (width <= 0 || height <= 0 || !jpegData || jpegSize == 0) return false;
	if (!EnsureInitialized()) return false;
	const auto start = std::chrono::steady_clock::now();

	MppApi* api = static_cast<MppApi*>(m_Api);
	MppCtx ctx = static_cast<MppCtx>(m_Ctx);

	// input packet: a real MppBuffer (the JPEG VPU DMAs from its input; a heap pointer won't do), from a dedicated group
	// sized to the known JPEG byte count.
	if (!EnsureInputBufferGroup(jpegSize)) return false;
	MppBuffer inputBuffer = nullptr;
	if (mpp_buffer_get(static_cast<MppBufferGroup>(m_InputBufGroup), &inputBuffer, jpegSize) != MPP_OK || !inputBuffer) {
		return false;
	}
	void* inputPtr = mpp_buffer_get_ptr(inputBuffer);
	if (!inputPtr) { mpp_buffer_put(inputBuffer); return false; }
	memcpy(inputPtr, jpegData, jpegSize);

	MppPacket packet = nullptr;
	if (mpp_packet_init_with_buffer(&packet, inputBuffer) != MPP_OK) {
		mpp_buffer_put(inputBuffer);
		return false;
	}
	// init_with_buffer defaults the packet length to the whole buffer capacity, which may exceed this frame; the decoder must
	// see only this frame's byte count.
	mpp_packet_set_length(packet, jpegSize);

	// output frame: pre-allocate the MppFrame and its MppBuffer from the known width/height and attach it to the packet's
	// metadata; MPP decodes directly into it. Strides align to 16 and the buffer is 4x w*h, enough for 4:2:0 or 4:2:2.
	RK_U32 horStride = MppAlign(static_cast<RK_U32>(width), 16);
	RK_U32 verStride = MppAlign(static_cast<RK_U32>(height), 16);
	size_t outBufSize = static_cast<size_t>(horStride) * verStride * 4;

	bool ok = false;
	if (EnsureOutputBufferGroup(outBufSize)) {
		MppBuffer outputBuffer = nullptr;
		if (mpp_buffer_get(static_cast<MppBufferGroup>(m_BufGroup), &outputBuffer, outBufSize) == MPP_OK && outputBuffer) {
			MppFrame frame = nullptr;
			if (mpp_frame_init(&frame) == MPP_OK) {
				mpp_frame_set_buffer(frame, outputBuffer);
				// the frame holds its own reference to outputBuffer.
				mpp_buffer_put(outputBuffer);

				MppMeta meta = mpp_packet_get_meta(packet);
				if (meta) mpp_meta_set_frame(meta, KEY_OUTPUT_FRAME, frame);

				if (api->decode_put_packet(ctx, packet) == MPP_OK) {
					MppFrame frameOut = nullptr;
					if (api->decode_get_frame(ctx, &frameOut) == MPP_OK && frameOut) {
						// Only 4:2:0 (NV12) output is handled (see MppJpegDecoder.h). A decode error, discarded frame or other chroma
						// layout (e.g. NV16) falls back to the caller's software path.
						if (mpp_frame_get_errinfo(frameOut) == 0 && mpp_frame_get_discard(frameOut) == 0 &&
							mpp_frame_get_fmt(frameOut) == MPP_FMT_YUV420SP) {
							int frameWidth = static_cast<int>(mpp_frame_get_width(frameOut));
							int frameHeight = static_cast<int>(mpp_frame_get_height(frameOut));
							int frameHorStride = static_cast<int>(mpp_frame_get_hor_stride(frameOut));
							int frameVerStride = static_cast<int>(mpp_frame_get_ver_stride(frameOut));
							MppBuffer frameBuffer = mpp_frame_get_buffer(frameOut);
							uint8_t* base = frameBuffer ? static_cast<uint8_t*>(mpp_buffer_get_ptr(frameBuffer)) : nullptr;

							// a size mismatch falls back rather than reading a mis-sized view (as the software MJPEG path does)
							if (base && frameWidth >= width && frameHeight >= height && frameHorStride >= width && frameVerStride >= height &&
								width % 2 == 0 && height % 2 == 0) {
								m_LastHwUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
								// the CPU will read the buffer: invalidate its cache lines once, now that the decoder has written it
								// (the buffer is cacheable); the planes are then read-only for everyone
								mpp_buffer_sync_ro_begin(frameBuffer);
								mpp_buffer_sync_ro_end(frameBuffer);
								// the chroma plane starts after verStride luma rows, which can be more than the picture's height
								out.y = cv::Mat(height, width, CV_8UC1, base, static_cast<size_t>(frameHorStride));
								out.uv = cv::Mat(height / 2, width / 2, CV_8UC2, base + static_cast<size_t>(frameHorStride) * frameVerStride,
									static_cast<size_t>(frameHorStride));
								// our own reference to the buffer, dropped when the last copy of `owner` is: it outlives the decoder's frame below
								mpp_buffer_inc_ref(frameBuffer);
								out.owner = std::shared_ptr<void>(frameBuffer, [group = m_OutGroupHolder](void* buffer) { mpp_buffer_put(static_cast<MppBuffer>(buffer)); });
								ok = true;
							}
						}
						// per the advanced interface, frameOut IS frame (attached above via packet metadata): deinit once, not twice.
						mpp_frame_deinit(&frameOut);
					} else {
						mpp_frame_deinit(&frame);
					}
				} else {
					mpp_frame_deinit(&frame);
				}
			} else {
				mpp_buffer_put(outputBuffer);
			}
		}
	}

	mpp_packet_deinit(&packet);
	// drops this function's own reference from mpp_buffer_get; mpp_packet_deinit already released the packet's separate one.
	mpp_buffer_put(inputBuffer);
	return ok;
}
#endif // LUMEN_WITH_MPP_JPEG
