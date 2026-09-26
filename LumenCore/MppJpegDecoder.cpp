#ifdef LUMEN_WITH_MPP_JPEG
#include "MppJpegDecoder.h"

#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/rk_vdec_cfg.h>

// TEMPORARY - tracing to pinpoint the exact crashing call on real hardware (Catch2's signal
// handler catches the SIGSEGV as a test failure, but gives no line/stack info of its own).
#include <cstdio>
#define MPPDBG(msg) do { fprintf(stderr, "MPPDBG %s:%d %s\n", __FILE__, __LINE__, msg); fflush(stderr); } while (0)

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
}

MppJpegDecoder::~MppJpegDecoder()
{
	// context first, then the buffer group it was using - putting the group first would free
	// memory the decoder might still touch during its own teardown.
	if (m_Ctx) mpp_destroy(static_cast<MppCtx>(m_Ctx));
	if (m_BufGroup) mpp_buffer_group_put(static_cast<MppBufferGroup>(m_BufGroup));
}

bool MppJpegDecoder::SetupBufferGroup(size_t bufSize)
{
	MPPDBG("SetupBufferGroup: enter");
	if (m_BufGroup && m_BufGroupSize >= bufSize) { MPPDBG("SetupBufferGroup: already big enough"); return true; }

	if (m_BufGroup) {
		MPPDBG("SetupBufferGroup: putting old group");
		mpp_buffer_group_put(static_cast<MppBufferGroup>(m_BufGroup));
		m_BufGroup = nullptr;
		m_BufGroupSize = 0;
	}

	MppApi* api = static_cast<MppApi*>(m_Api);
	MppCtx ctx = static_cast<MppCtx>(m_Ctx);

	// priority order per mpp_buffer.h's own comment ("MPP_BUFFER_TYPE_DMA_HEAP >
	// MPP_BUFFER_TYPE_DRM > MPP_BUFFER_TYPE_ION") - fall back down the list if the preferred
	// allocator isn't available on this kernel rather than failing outright.
	static const MppBufferType kTypesInPriorityOrder[] = {
		MPP_BUFFER_TYPE_DMA_HEAP, MPP_BUFFER_TYPE_DRM, MPP_BUFFER_TYPE_ION
	};
	MppBufferGroup group = nullptr;
	for (MppBufferType type : kTypesInPriorityOrder) {
		MPPDBG("SetupBufferGroup: trying mpp_buffer_group_get");
		if (mpp_buffer_group_get(&group, type, MPP_BUFFER_INTERNAL, "lumen_mpp_jpeg", __func__) == MPP_OK && group) {
			MPPDBG("SetupBufferGroup: mpp_buffer_group_get succeeded");
			break;
		}
		group = nullptr;
	}
	if (!group) { MPPDBG("SetupBufferGroup: no allocator type worked"); return false; }

	MPPDBG("SetupBufferGroup: calling mpp_buffer_group_limit_config");
	if (mpp_buffer_group_limit_config(group, bufSize, kBufferCount) != MPP_OK) {
		MPPDBG("SetupBufferGroup: limit_config failed");
		mpp_buffer_group_put(group);
		return false;
	}
	MPPDBG("SetupBufferGroup: limit_config ok, calling control(SET_EXT_BUF_GROUP)");
	if (api->control(ctx, MPP_DEC_SET_EXT_BUF_GROUP, group) != MPP_OK) {
		MPPDBG("SetupBufferGroup: control(SET_EXT_BUF_GROUP) failed");
		mpp_buffer_group_put(group);
		return false;
	}
	MPPDBG("SetupBufferGroup: control(SET_EXT_BUF_GROUP) ok");

	m_BufGroup = group;
	m_BufGroupSize = bufSize;
	MPPDBG("SetupBufferGroup: exit ok");
	return true;
}

bool MppJpegDecoder::EnsureInitialized()
{
	if (m_InitAttempted) return m_InitOk;
	m_InitAttempted = true;

	MppCtx ctx = nullptr;
	MppApi* api = nullptr;
	MPPDBG("EnsureInitialized: calling mpp_create");
	if (mpp_create(&ctx, &api) != MPP_OK) { MPPDBG("EnsureInitialized: mpp_create failed"); return false; }
	MPPDBG("EnsureInitialized: mpp_create ok, calling mpp_init");
	if (mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG) != MPP_OK) {
		MPPDBG("EnsureInitialized: mpp_init failed");
		mpp_destroy(ctx);
		return false;
	}
	MPPDBG("EnsureInitialized: mpp_init ok");

	// CONFIRMED THE HARD WAY (a real board segfault, isolated down to the very first api->decode
	// call, before this class's own buffer-group setup even runs): a decode context needs its
	// MppDecCfg fetched, configured and re-applied before ANY decode call, exactly like the real
	// reference (rockchip-linux/mpp's own test/mpi_dec_test.c) always does - skipping this
	// entirely (this class's own prior state) leaves the decoder in a state its own first
	// decode() call cannot handle. "base:split_parse"=1 matches the demo's own default (lets
	// MPP's internal frame splitter find frame boundaries) - a no-op for a single already-
	// complete JPEG image, but this is the documented, tested init sequence, not a value chosen
	// for its own meaning.
	MppDecCfg cfg = nullptr;
	MPPDBG("EnsureInitialized: calling mpp_dec_cfg_init");
	if (mpp_dec_cfg_init(&cfg) != MPP_OK) {
		MPPDBG("EnsureInitialized: mpp_dec_cfg_init failed");
		mpp_destroy(ctx);
		return false;
	}
	MPPDBG("EnsureInitialized: mpp_dec_cfg_init ok, calling control(GET_CFG)");
	if (api->control(ctx, MPP_DEC_GET_CFG, cfg) != MPP_OK) {
		MPPDBG("EnsureInitialized: control(GET_CFG) failed");
		mpp_dec_cfg_deinit(cfg);
		mpp_destroy(ctx);
		return false;
	}
	MPPDBG("EnsureInitialized: control(GET_CFG) ok, calling cfg_set_u32(split_parse)");
	mpp_dec_cfg_set_u32(cfg, "base:split_parse", 1);
	MPPDBG("EnsureInitialized: cfg_set_u32 ok, calling control(SET_CFG)");
	if (api->control(ctx, MPP_DEC_SET_CFG, cfg) != MPP_OK) {
		MPPDBG("EnsureInitialized: control(SET_CFG) failed");
		mpp_dec_cfg_deinit(cfg);
		mpp_destroy(ctx);
		return false;
	}
	MPPDBG("EnsureInitialized: control(SET_CFG) ok, deiniting cfg");
	mpp_dec_cfg_deinit(cfg);
	MPPDBG("EnsureInitialized: exit ok");

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

	MPPDBG("Decode: enter, calling mpp_packet_init");
	MppPacket packet = nullptr;
	// no copy - wraps the caller's own buffer (the same V4L2 mmap'd bytes Grab() already has),
	// exactly like the software cv::imdecode path one level up.
	if (mpp_packet_init(&packet, const_cast<uint8_t*>(jpegData), jpegSize) != MPP_OK) { MPPDBG("Decode: mpp_packet_init failed"); return false; }
	MPPDBG("Decode: mpp_packet_init ok");

	bool ok = false;
	for (int attempt = 0; attempt < kMaxDecodeAttempts && !ok; attempt++) {
		MppFrame frame = nullptr;
		MPPDBG("Decode: calling api->decode");
		if (api->decode(ctx, packet, &frame) != MPP_OK || !frame) { MPPDBG("Decode: api->decode failed or no frame"); break; }
		MPPDBG("Decode: api->decode ok, frame non-null");

		MPPDBG("Decode: calling mpp_frame_get_info_change");
		if (mpp_frame_get_info_change(frame)) {
			MPPDBG("Decode: info_change true, calling mpp_frame_get_buf_size");
			size_t bufSize = mpp_frame_get_buf_size(frame);
			MPPDBG("Decode: got buf_size, calling SetupBufferGroup");
			// set up (or grow) the buffer group the decoder needs BEFORE acknowledging - see this
			// file's own top comment on why this is mandatory, not optional. A failure here (no
			// supported allocator, group setup rejected) falls straight back to software rather
			// than acknowledging into a decoder that has nowhere to put its output.
			if (!SetupBufferGroup(bufSize)) {
				MPPDBG("Decode: SetupBufferGroup failed");
				mpp_frame_deinit(&frame);
				break;
			}
			MPPDBG("Decode: SetupBufferGroup ok, calling control(INFO_CHANGE_READY)");
			api->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, nullptr);
			MPPDBG("Decode: control(INFO_CHANGE_READY) ok, deiniting frame");
			mpp_frame_deinit(&frame);
			MPPDBG("Decode: frame deinit ok, looping");
			continue;
		}
		MPPDBG("Decode: info_change false - real frame");

		// Only 4:2:0 (NV12) output is handled - see this class's own header comment. A decode
		// error/discarded frame, or any other reported chroma layout (4:2:2/NV16 for a 4:2:2
		// JPEG), falls back to the caller's software path rather than guessing at a conversion
		// OpenCV has no built-in cvtColor code for.
		MPPDBG("Decode: checking errinfo/discard/fmt");
		if (mpp_frame_get_errinfo(frame) != 0 || mpp_frame_get_discard(frame) != 0 ||
			mpp_frame_get_fmt(frame) != MPP_FMT_YUV420SP) {
			MPPDBG("Decode: errinfo/discard/fmt rejected this frame");
			mpp_frame_deinit(&frame);
			break;
		}

		MPPDBG("Decode: reading width/height/hor_stride/buffer");
		int frameWidth = static_cast<int>(mpp_frame_get_width(frame));
		int frameHeight = static_cast<int>(mpp_frame_get_height(frame));
		int horStride = static_cast<int>(mpp_frame_get_hor_stride(frame));
		MppBuffer buffer = mpp_frame_get_buffer(frame);
		MPPDBG("Decode: calling mpp_buffer_get_ptr");
		const uint8_t* base = buffer ? static_cast<const uint8_t*>(mpp_buffer_get_ptr(buffer)) : nullptr;
		MPPDBG("Decode: got base pointer");

		// a genuine size mismatch (a driver/decoder surprise) falls back rather than reading a
		// mis-sized view into the real buffer - same discipline the software MJPEG path already
		// uses (see V4l2CameraBackend.cpp's own comment on this).
		if (!base || frameWidth < width || frameHeight < height || horStride < width) {
			MPPDBG("Decode: size/base mismatch, rejecting frame");
			mpp_frame_deinit(&frame);
			break;
		}

		// `dst` is already the caller's own FramePool-acquired buffer, right-sized/typed for
		// asGray - this class never touches FramePool itself, see this file's own header
		// comment on why.
		if (asGray) {
			MPPDBG("Decode: asGray copyTo");
			// the Y plane IS the grayscale image - genuinely free, no colour conversion at all.
			cv::Mat yView(height, width, CV_8UC1, const_cast<uint8_t*>(base), static_cast<size_t>(horStride));
			yView.copyTo(dst);
			MPPDBG("Decode: asGray copyTo done");
		} else {
			MPPDBG("Decode: cvtColor NV12->BGR");
			// same strided-view construction V4l2CameraBackend.cpp's own software NV12 branch
			// already uses for the wire format - one Mat spanning the Y plane (height rows)
			// directly followed by interleaved UV at half resolution, all at horStride.
			cv::Mat nv12View(height * 3 / 2, width, CV_8UC1, const_cast<uint8_t*>(base), static_cast<size_t>(horStride));
			cv::cvtColor(nv12View, dst, cv::COLOR_YUV2BGR_NV12);
			MPPDBG("Decode: cvtColor done");
		}

		MPPDBG("Decode: deiniting final frame");
		mpp_frame_deinit(&frame);
		ok = true;
		MPPDBG("Decode: ok = true");
	}

	MPPDBG("Decode: loop exited, calling mpp_packet_deinit");
	mpp_packet_deinit(&packet);
	MPPDBG("Decode: mpp_packet_deinit ok, returning");
	return ok;
}
#endif // LUMEN_WITH_MPP_JPEG
