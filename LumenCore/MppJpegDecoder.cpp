#ifdef LUMEN_WITH_MPP_JPEG
#include "MppJpegDecoder.h"

#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/rk_vdec_cfg.h>
#include <cstdio>
#include <cstring>

// NOT CURRENTLY WORKING ON REAL HARDWARE - see LumenCore/CMakeLists.txt's own comment on
// LUMEN_ENABLE_EXPERIMENTAL_MPP_JPEG (left OFF by default) for the full story. Short version:
// api->decode() below reliably segfaults inside librockchip_mpp.so's own mpp_dec_decode(), at the
// identical relative offset (+0x420, landing inside its mpp_parser_prepare() call - i.e. MPP's own
// internal JPEG bitstream parser) across two independent MPP versions and two structurally
// different buffer-handling approaches. That points at a real incompatibility between this
// board's vendor kernel/driver and MPP's JPEG decode path, not a bug in this calling code, but
// that has NOT been proven conclusively (no debug symbols/gdb were available on-device to confirm
// the exact faulting statement). DumpMapsOnce below is kept for whoever picks this up next - see
// its own comment for the kernel.print-fatal-signals correlation technique that localized the
// fault this precisely without a debugger.
namespace {
	// dumps this process's own /proc/self/maps to stderr - correlate the fault PC dmesg reports
	// (`sysctl -w kernel.print-fatal-signals=1`, then read the crash's pc/lr out of dmesg) against
	// this to find which .so and offset actually faulted, then `nm -D --defined-only <lib> | sort`
	// to find the nearest exported symbol at-or-below that offset.
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
}

namespace {
	// A fresh MJPEG decode context requires an "info change" round trip on its first real
	// picture (the decoder reports the buffer requirements it discovered from the bitstream
	// before it will actually produce pixels - see rockchip-linux/mpp's own test/mpi_dec_test.c,
	// the "simple decode" path this class otherwise mirrors) - MPP_DEC_SET_INFO_CHANGE_READY
	// acknowledges it and lets decoding continue. CONFIRMED THE HARD WAY (a real board segfault,
	// not a guess): an explicit buffer group registered via MPP_DEC_SET_EXT_BUF_GROUP is
	// mandatory here, even for a single-frame codec with no reference chaining - a prior version
	// of this class skipped it entirely (reasoning "JPEG needs no reference-frame pool, MPP's
	// internal allocation should be enough"), which crashed the whole process the moment a real
	// frame was decoded. mpi_dec_test.c's own default buffer mode (MPP_DEC_BUF_HALF_INT in its
	// own utils/mpi_dec_utils.c) always sets one up; SetupBufferGroup below mirrors exactly that
	// path (mpp_buffer_group_get_internal + mpp_buffer_group_limit_config), not the untested
	// "mode=INTERNAL, register nothing" alternative that same file also defines but never uses
	// by default. Bounded, not a real retry loop: one real info-change round trip is the
	// documented case; anything beyond that is treated as a protocol surprise this class doesn't
	// understand, not looped on forever.
	constexpr int kMaxDecodeAttempts = 4;

	// JPEG has no reference-frame chaining (one frame in, one frame out) - a handful of buffers
	// is plenty; this only bounds how many the group is ALLOWED to grow to
	// (mpp_buffer_group_limit_config), not a fixed pre-allocation.
	constexpr RK_S32 kBufferCount = 4;

	// shared by SetupBufferGroup (output frames) and EnsureInputBufferGroup (the input packet) -
	// priority order per mpp_buffer.h's own comment ("MPP_BUFFER_TYPE_DMA_HEAP >
	// MPP_BUFFER_TYPE_DRM > MPP_BUFFER_TYPE_ION") - fall back down the list if the preferred
	// allocator isn't available on this kernel rather than failing outright.
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

bool MppJpegDecoder::SetupBufferGroup(size_t bufSize)
{
	if (m_BufGroup && m_BufGroupSize >= bufSize) return true;

	if (m_BufGroup) {
		mpp_buffer_group_put(static_cast<MppBufferGroup>(m_BufGroup));
		m_BufGroup = nullptr;
		m_BufGroupSize = 0;
	}

	MppApi* api = static_cast<MppApi*>(m_Api);
	MppCtx ctx = static_cast<MppCtx>(m_Ctx);

	MppBufferGroup group = CreateInternalGroup("lumen_mpp_jpeg");
	if (!group) return false;

	if (mpp_buffer_group_limit_config(group, bufSize, kBufferCount) != MPP_OK) {
		mpp_buffer_group_put(group);
		return false;
	}
	if (api->control(ctx, MPP_DEC_SET_EXT_BUF_GROUP, group) != MPP_OK) {
		mpp_buffer_group_put(group);
		return false;
	}

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

	// CONFIRMED THE HARD WAY (a real board segfault, isolated down to the very first api->decode
	// call, before this class's own buffer-group setup even runs): a decode context needs its
	// MppDecCfg fetched, configured and re-applied before ANY decode call, exactly like the real
	// reference (rockchip-linux/mpp's own test/mpi_dec_test.c) always does - skipping this
	// entirely (this class's own prior state) leaves the decoder in a state its own first
	// decode() call cannot handle. Necessary but NOT sufficient - see this file's top comment;
	// the crash this was meant to fix moved past mpp_init but still happens later, inside decode()
	// itself, localized (kernel fault correlation) to right around mpp_dec_decode's own call into
	// mpp_parser_prepare() - the exact function whose behavior "base:split_parse" controls.
	// TRYING split_parse=0 here (was 1, matching mpi_dec_test.c's H.264-oriented default): 1 tells
	// MPP's parser to expect a continuous elementary stream and scan for frame boundaries across
	// calls (and this code never calls mpp_packet_set_eos, since it does one-shot decode, not
	// streaming - a mismatch with what split_parse=1 expects). This class always hands MPP exactly
	// one complete, self-contained JPEG image (SOI...EOI) per call - the case split_parse=0 (no
	// boundary-scanning, parse the packet directly as one complete frame) is actually for.
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

	// CONFIRMED THE HARD WAY (isolated via a HITL test with no camera/Server involved, then a
	// kernel-level fault report - kernel.print-fatal-signals=1 - correlated against
	// /proc/self/maps): an earlier version of this function crashed on the very FIRST
	// api->decode() call, before this class's own output buffer group even got a chance to run,
	// because mpp_packet_init() wrapped a plain heap pointer with no MppBuffer behind it - the
	// JPEG-decode VPU needs to DMA directly from its input, which a plain heap pointer was never
	// going to satisfy. The input packet now uses a real MppBuffer (from its own dedicated buffer
	// group, since its size - the JPEG's own byte count - is known upfront, unlike the output
	// frame's, which the decoder only reveals via its first info-change), with the JPEG bytes
	// copied in - the same "real MppBuffer, not a bare pointer" principle SetupBufferGroup already
	// applies on the output side. This fixed THAT crash, but api->decode() below still crashes
	// later, deeper inside mpp_dec_decode() itself - see this file's top comment.
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

	bool ok = false;
	for (int attempt = 0; attempt < kMaxDecodeAttempts && !ok; attempt++) {
		MppFrame frame = nullptr;
		// see this file's top comment - api->decode() is where this reliably segfaults on real
		// hardware today. DumpMapsOnce below is what localized that to mpp_dec_decode+0x420.
		DumpMapsOnce();
		if (api->decode(ctx, packet, &frame) != MPP_OK || !frame) break;

		if (mpp_frame_get_info_change(frame)) {
			size_t bufSize = mpp_frame_get_buf_size(frame);
			// set up (or grow) the buffer group the decoder needs BEFORE acknowledging - see this
			// file's own top comment on why this is mandatory, not optional. A failure here (no
			// supported allocator, group setup rejected) falls straight back to software rather
			// than acknowledging into a decoder that has nowhere to put its output.
			if (!SetupBufferGroup(bufSize)) {
				mpp_frame_deinit(&frame);
				break;
			}
			api->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, nullptr);
			mpp_frame_deinit(&frame);
			continue;
		}

		// Only 4:2:0 (NV12) output is handled - see this class's own header comment. A decode
		// error/discarded frame, or any other reported chroma layout (4:2:2/NV16 for a 4:2:2
		// JPEG), falls back to the caller's software path rather than guessing at a conversion
		// OpenCV has no built-in cvtColor code for.
		if (mpp_frame_get_errinfo(frame) != 0 || mpp_frame_get_discard(frame) != 0 ||
			mpp_frame_get_fmt(frame) != MPP_FMT_YUV420SP) {
			mpp_frame_deinit(&frame);
			break;
		}

		int frameWidth = static_cast<int>(mpp_frame_get_width(frame));
		int frameHeight = static_cast<int>(mpp_frame_get_height(frame));
		int horStride = static_cast<int>(mpp_frame_get_hor_stride(frame));
		MppBuffer buffer = mpp_frame_get_buffer(frame);
		const uint8_t* base = buffer ? static_cast<const uint8_t*>(mpp_buffer_get_ptr(buffer)) : nullptr;

		// a genuine size mismatch (a driver/decoder surprise) falls back rather than reading a
		// mis-sized view into the real buffer - same discipline the software MJPEG path already
		// uses (see V4l2CameraBackend.cpp's own comment on this).
		if (!base || frameWidth < width || frameHeight < height || horStride < width) {
			mpp_frame_deinit(&frame);
			break;
		}

		// `dst` is already the caller's own FramePool-acquired buffer, right-sized/typed for
		// asGray - this class never touches FramePool itself, see this file's own header
		// comment on why.
		if (asGray) {
			// the Y plane IS the grayscale image - genuinely free, no colour conversion at all.
			cv::Mat yView(height, width, CV_8UC1, const_cast<uint8_t*>(base), static_cast<size_t>(horStride));
			yView.copyTo(dst);
		} else {
			// same strided-view construction V4l2CameraBackend.cpp's own software NV12 branch
			// already uses for the wire format - one Mat spanning the Y plane (height rows)
			// directly followed by interleaved UV at half resolution, all at horStride.
			cv::Mat nv12View(height * 3 / 2, width, CV_8UC1, const_cast<uint8_t*>(base), static_cast<size_t>(horStride));
			cv::cvtColor(nv12View, dst, cv::COLOR_YUV2BGR_NV12);
		}

		mpp_frame_deinit(&frame);
		ok = true;
	}

	mpp_packet_deinit(&packet);
	// drops THIS function's own reference from mpp_buffer_get above - mpp_packet_init_with_buffer
	// took its own separate reference for the packet, already released by mpp_packet_deinit.
	mpp_buffer_put(inputBuffer);
	return ok;
}
#endif // LUMEN_WITH_MPP_JPEG
