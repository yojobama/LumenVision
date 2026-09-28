#ifdef LUMEN_WITH_MPP_JPEG
#include "MppJpegDecoder.h"

#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_meta.h>
#include <rockchip/rk_vdec_cfg.h>
#include <cstdio>
#include <cstring>

// PREVIOUSLY NOT WORKING ON REAL HARDWARE, root-caused and fixed - see the git history around
// "Conclude MPP JPEG hardware decode investigation" and the mpp-jpeg-decode-crash memory for the
// full trail. Short version: the earlier version of this class called api->decode(ctx, packet,
// &frame) - the "simple" synchronous decode API - which reliably segfaulted deep inside
// librockchip_mpp.so's own mpp_dec_decode(), at the identical relative offset across two MPP
// versions, two buffer-handling approaches, and both values of "base:split_parse". None of that
// was the real bug: MPP's OWN reference test explicitly never uses the simple decode() API for
// MJPEG at all (rockchip-linux/mpp's test/mpi_dec_test.c: `cmd->simple = (cmd->type !=
// MPP_VIDEO_CodingMJPEG) ? (1) : (0);`), and a maintainer confirmed this directly when asked
// (https://github.com/rockchip-linux/mpp/issues/586: "是的，jpeg 解码走 advanced 接口" - "Yes, JPEG
// decoding goes through the advanced interface"). This class now uses that "advanced" interface
// instead: decode_put_packet()+decode_get_frame(), with the OUTPUT frame buffer pre-allocated by
// this class itself (from the width/height the caller already knows) and attached to the packet
// via its own metadata BEFORE decoding - no info-change negotiation round trip at all, unlike the
// simple API's contract other codecs use.
namespace {
	// dumps this process's own /proc/self/maps to stderr - correlate the fault PC dmesg reports
	// (`sysctl -w kernel.print-fatal-signals=1`, then read the crash's pc/lr out of dmesg) against
	// this to find which .so and offset actually faulted, then `nm -D --defined-only <lib> | sort`
	// to find the nearest exported symbol at-or-below that offset. Kept from the investigation
	// that root-caused this - harmless if nothing ever crashes here again.
	void DumpMapsOnce() {
		static bool done = false;
		if (done) return;
		done = true;
		FILE* f = fopen("/proc/self/maps", "r");
		if (!f) return;
		fprintf(stderr, "MppJpegDecoder: /proc/self/maps --\n");
		char line[512];
		while (fgets(line, sizeof(line), f)) fputs(line, stderr);
		fprintf(stderr, "MppJpegDecoder: -- end maps\n");
		fflush(stderr);
		fclose(f);
	}

	// MPP_ALIGN as defined by rockchip-linux/mpp's own osal/inc/mpp_common.h - not part of the
	// public pkg-config include path, so defined locally rather than depending on an internal
	// header. Only ever used with a=16 here, matching mpi_dec_test.c's own advanced-path sizing.
	constexpr RK_U32 MppAlign(RK_U32 x, RK_U32 a) { return (x + a - 1) & ~(a - 1); }

	// shared by EnsureOutputBufferGroup and EnsureInputBufferGroup - priority order per
	// mpp_buffer.h's own comment ("MPP_BUFFER_TYPE_DMA_HEAP > MPP_BUFFER_TYPE_DRM >
	// MPP_BUFFER_TYPE_ION") - fall back down the list if the preferred allocator isn't available
	// on this kernel rather than failing outright.
	MppBufferGroup CreateInternalGroup(const char* tag)
	{
		static const MppBufferType kTypesInPriorityOrder[] = {
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
	// context first, then the buffer groups it was using - putting a group first would free
	// memory the decoder might still touch during its own teardown.
	if (m_Ctx) mpp_destroy(static_cast<MppCtx>(m_Ctx));
	if (m_BufGroup) mpp_buffer_group_put(static_cast<MppBufferGroup>(m_BufGroup));
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
	MppBufferGroup group = CreateInternalGroup("lumen_mpp_jpeg_in");
	if (!group) return false;
	m_InputBufGroup = group;
	m_InputBufGroupSize = jpegSize;
	return true;
}

bool MppJpegDecoder::EnsureOutputBufferGroup(size_t bufSize)
{
	if (m_BufGroup && m_BufGroupSize >= bufSize) return true;
	if (m_BufGroup) {
		mpp_buffer_group_put(static_cast<MppBufferGroup>(m_BufGroup));
		m_BufGroup = nullptr;
		m_BufGroupSize = 0;
	}
	MppBufferGroup group = CreateInternalGroup("lumen_mpp_jpeg_out");
	if (!group) return false;
	m_BufGroup = group;
	m_BufGroupSize = bufSize;
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

	// A decode context needs its MppDecCfg fetched, configured and re-applied before ANY decode
	// call, exactly like the real reference (rockchip-linux/mpp's own test/mpi_dec_test.c) always
	// does. "base:split_parse"=0 is the semantically correct value for this class's one-shot
	// calling pattern (one complete, self-contained JPEG per call, no cross-call boundary
	// scanning) - confirmed to make no difference to the crash that used to happen here (that
	// crash was the simple decode() API itself being wrong for MJPEG, not this flag - see this
	// file's top comment), but there's no reason to use the semantically wrong value now that
	// this class no longer needs to match the H.264-oriented demo default.
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

	m_Ctx = ctx;
	m_Api = api;
	m_InitOk = true;
	return true;
}

bool MppJpegDecoder::Decode(const uint8_t* jpegData, size_t jpegSize, int width, int height, bool asGray, cv::Mat& dst)
{
	if (width <= 0 || height <= 0 || !jpegData || jpegSize == 0) return false;
	if (!EnsureInitialized()) return false;

	MppApi* api = static_cast<MppApi*>(m_Api);
	MppCtx ctx = static_cast<MppCtx>(m_Ctx);

	// input packet: a real MppBuffer (the JPEG-decode VPU DMAs directly from its input, which a
	// bare heap pointer can't satisfy), from its own dedicated buffer group since its size - the
	// JPEG's own byte count - is known upfront.
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
	// init_with_buffer defaults the packet's length to the WHOLE buffer's capacity (which may be
	// larger than this exact frame once the group's own buffer is reused/regrown) - the decoder
	// must only see this frame's real byte count.
	mpp_packet_set_length(packet, jpegSize);

	// output frame: per the advanced interface's contract (see this file's top comment), THIS
	// class pre-allocates the output MppFrame and its backing MppBuffer from the width/height the
	// caller already knows, and attaches it to the packet's own metadata before decoding - MPP
	// decodes directly into it, with no info-change negotiation at all. Sizing/alignment mirrors
	// mpi_dec_test.c's own dec_advanced() setup exactly (hor_stride/ver_stride aligned to 16,
	// buffer sized at 4x w*h - enough headroom for either 4:2:0 or 4:2:2 chroma).
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
				// the frame now holds its own reference to outputBuffer.
				mpp_buffer_put(outputBuffer);

				MppMeta meta = mpp_packet_get_meta(packet);
				if (meta) mpp_meta_set_frame(meta, KEY_OUTPUT_FRAME, frame);

				DumpMapsOnce();
				if (api->decode_put_packet(ctx, packet) == MPP_OK) {
					MppFrame frameOut = nullptr;
					if (api->decode_get_frame(ctx, &frameOut) == MPP_OK && frameOut) {
						// Only 4:2:0 (NV12) output is handled - see this class's own header
						// comment. A decode error/discarded frame, or any other reported chroma
						// layout (4:2:2/NV16 for a 4:2:2 JPEG), falls back to the caller's
						// software path rather than guessing at a conversion OpenCV has no
						// built-in cvtColor code for.
						if (mpp_frame_get_errinfo(frameOut) == 0 && mpp_frame_get_discard(frameOut) == 0 &&
							mpp_frame_get_fmt(frameOut) == MPP_FMT_YUV420SP) {
							int frameWidth = static_cast<int>(mpp_frame_get_width(frameOut));
							int frameHeight = static_cast<int>(mpp_frame_get_height(frameOut));
							int frameHorStride = static_cast<int>(mpp_frame_get_hor_stride(frameOut));
							MppBuffer frameBuffer = mpp_frame_get_buffer(frameOut);
							const uint8_t* base = frameBuffer ? static_cast<const uint8_t*>(mpp_buffer_get_ptr(frameBuffer)) : nullptr;

							// a genuine size mismatch (a driver/decoder surprise) falls back
							// rather than reading a mis-sized view into the real buffer - same
							// discipline the software MJPEG path already uses (see
							// V4l2CameraBackend.cpp's own comment on this).
							if (base && frameWidth >= width && frameHeight >= height && frameHorStride >= width) {
								// `dst` is already the caller's own FramePool-acquired buffer,
								// right-sized/typed for asGray - this class never touches
								// FramePool itself, see this file's own header comment on why.
								if (asGray) {
									// the Y plane IS the grayscale image - genuinely free, no
									// colour conversion at all.
									cv::Mat yView(height, width, CV_8UC1, const_cast<uint8_t*>(base), static_cast<size_t>(frameHorStride));
									yView.copyTo(dst);
								} else {
									// same strided-view construction V4l2CameraBackend.cpp's own
									// software NV12 branch already uses for the wire format - one
									// Mat spanning the Y plane (height rows) directly followed by
									// interleaved UV at half resolution, all at frameHorStride.
									cv::Mat nv12View(height * 3 / 2, width, CV_8UC1, const_cast<uint8_t*>(base), static_cast<size_t>(frameHorStride));
									cv::cvtColor(nv12View, dst, cv::COLOR_YUV2BGR_NV12);
								}
								ok = true;
							}
						}
						// per the advanced interface's contract, frameOut IS frame (the same
						// preallocated frame attached above via the packet's own metadata) - one
						// deinit for both, not two, or this double-frees.
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
	// drops THIS function's own reference from mpp_buffer_get above - mpp_packet_init_with_buffer
	// took its own separate reference for the packet, already released by mpp_packet_deinit.
	mpp_buffer_put(inputBuffer);
	return ok;
}
#endif // LUMEN_WITH_MPP_JPEG
