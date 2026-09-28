#include "V4l2CameraBackend.h"
#include "SourceResult.h"
#include "FramePool.h"

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <linux/videodev2.h>
#include <cstring>
#include <errno.h>
#include <optional>
#include <utility>
#include <chrono>
#include <cstdint>

#include "PixelUnpack.h"

// newer than some distro kernel headers this project builds against - the values are fixed by
// the V4L2 ABI, so defining them here is safe
#ifndef V4L2_PIX_FMT_Y10P
#define V4L2_PIX_FMT_Y10P v4l2_fourcc('Y', '1', '0', 'P')
#endif
#ifndef V4L2_PIX_FMT_Y10BPACK
#define V4L2_PIX_FMT_Y10BPACK v4l2_fourcc('Y', '1', '0', 'B')
#endif
#ifndef V4L2_CID_ANALOGUE_GAIN
#define V4L2_CID_ANALOGUE_GAIN (V4L2_CID_IMAGE_SOURCE_CLASS_BASE + 3)
#endif

namespace {
	constexpr int BUFFER_COUNT = 4;
	// bounded well above any real frame interval so Grab() never mistakes a slow camera for a hung one, yet returns
	// promptly for ISource::Toggle(false)/shutdown
	constexpr int POLL_TIMEOUT_MS = 1000;

	int XIoctl(int fd, unsigned long request, void* arg)
	{
		int ret;
		// EINTR is routine (a signal interrupting the syscall); retry rather than reporting an ioctl error
		do {
			ret = ioctl(fd, request, arg);
		} while (ret == -1 && errno == EINTR);
		return ret;
	}

	// only formats Grab() can turn into pixels; anything else is left out of EnumerateModes rather than mislabelled
	std::optional<FrameFormat> FourCcToFrameFormat(uint32_t fourcc)
	{
		switch (fourcc) {
		case V4L2_PIX_FMT_MJPEG:
		case V4L2_PIX_FMT_JPEG:
			return FrameFormat::MJPEG;
		case V4L2_PIX_FMT_YUYV:
			return FrameFormat::YUYV;
		case V4L2_PIX_FMT_NV12:
			return FrameFormat::NV12;
		case V4L2_PIX_FMT_BGR24:
			return FrameFormat::BGR24;
		case V4L2_PIX_FMT_RGB24:
			return FrameFormat::RGB24;
		case V4L2_PIX_FMT_GREY:
			return FrameFormat::GRAY8;
		case V4L2_PIX_FMT_Y10:
			return FrameFormat::Y10;
		case V4L2_PIX_FMT_Y16:
			return FrameFormat::Y16;
		case V4L2_PIX_FMT_Y10P:
			return FrameFormat::Y10P;
		case V4L2_PIX_FMT_Y10BPACK:
			return FrameFormat::Y10BPACK;
		default:
			return std::nullopt;
		}
	}

	uint32_t FrameFormatToFourCc(FrameFormat format)
	{
		switch (format) {
		case FrameFormat::MJPEG: return V4L2_PIX_FMT_MJPEG;
		case FrameFormat::YUYV: return V4L2_PIX_FMT_YUYV;
		case FrameFormat::NV12: return V4L2_PIX_FMT_NV12;
		case FrameFormat::BGR24: return V4L2_PIX_FMT_BGR24;
		case FrameFormat::RGB24: return V4L2_PIX_FMT_RGB24;
		case FrameFormat::GRAY8: return V4L2_PIX_FMT_GREY;
		case FrameFormat::Y10: return V4L2_PIX_FMT_Y10;
		case FrameFormat::Y16: return V4L2_PIX_FMT_Y16;
		case FrameFormat::Y10P: return V4L2_PIX_FMT_Y10P;
		case FrameFormat::Y10BPACK: return V4L2_PIX_FMT_Y10BPACK;
		default: return V4L2_PIX_FMT_MJPEG;
		}
	}

	// Sizes offered for a STEPWISE/CONTINUOUS frame-size range (raw-sensor drivers report one range, not a list):
	// common resolutions plus the range's maximum.
	constexpr std::pair<uint32_t, uint32_t> kCommonFrameSizes[] = {
		{320, 240}, {640, 400}, {640, 480}, {800, 600}, {1280, 720},
		{1280, 800}, {1600, 1200}, {1920, 1080}, {1920, 1200},
	};

	bool FitsStepwise(const v4l2_frmsize_stepwise& range, uint32_t width, uint32_t height)
	{
		if (width < range.min_width || width > range.max_width) return false;
		if (height < range.min_height || height > range.max_height) return false;
		// a zero step is invalid per spec but seen in the wild - treat it as continuous
		if (range.step_width > 0 && (width - range.min_width) % range.step_width != 0) return false;
		if (range.step_height > 0 && (height - range.min_height) % range.step_height != 0) return false;
		return true;
	}
}

V4l2CameraBackend::~V4l2CameraBackend()
{
	Close();
}

bool V4l2CameraBackend::Open(const std::string& devicePath)
{
	Close();

	// O_NONBLOCK keeps VIDIOC_DQBUF from blocking indefinitely if a spurious poll() wakeup races an empty queue
	m_Fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK);
	if (m_Fd < 0) return false;

	v4l2_capability cap{};
	if (XIoctl(m_Fd, VIDIOC_QUERYCAP, &cap) < 0 ||
		!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
		!(cap.capabilities & V4L2_CAP_STREAMING)) {
		close(m_Fd);
		m_Fd = -1;
		return false;
	}

	m_DevicePath = devicePath;

	// Default so callers that never call SetMode() still get frames: MJPEG at a modest resolution, else the camera's first
	// advertised mode (raw mono sensors). If the request fails, ApplyFormat keeps the driver's default size and Open() still succeeds.
	std::vector<CameraMode> modes = EnumerateModes();
	bool hasMjpeg = false;
	for (const CameraMode& mode : modes) hasMjpeg |= mode.pixelFormat == FrameFormat::MJPEG;
	if (hasMjpeg || modes.empty()) {
		ApplyFormat(640, 480, V4L2_PIX_FMT_MJPEG);
	} else {
		ApplyFormat(modes[0].width, modes[0].height, FrameFormatToFourCc(modes[0].pixelFormat));
	}

	return StartStreaming();
}

void V4l2CameraBackend::Close()
{
	StopStreaming();
	if (m_Fd >= 0) {
		close(m_Fd);
		m_Fd = -1;
	}
}

bool V4l2CameraBackend::IsOpened() const
{
	return m_Fd >= 0;
}

bool V4l2CameraBackend::ApplyFormat(int width, int height, uint32_t fourcc)
{
	if (m_Fd < 0) return false;

	v4l2_format fmt{};
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = width;
	fmt.fmt.pix.height = height;
	fmt.fmt.pix.pixelformat = fourcc;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;

	return XIoctl(m_Fd, VIDIOC_S_FMT, &fmt) >= 0;
}

bool V4l2CameraBackend::StartStreaming()
{
	if (m_Streaming) return true;
	if (m_Fd < 0) return false;

	v4l2_requestbuffers req{};
	req.count = BUFFER_COUNT;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (XIoctl(m_Fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 1) return false;

	m_Buffers.resize(req.count);
	for (unsigned int i = 0; i < req.count; i++) {
		v4l2_buffer buf{};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (XIoctl(m_Fd, VIDIOC_QUERYBUF, &buf) < 0) {
			m_Buffers.clear();
			return false;
		}

		m_Buffers[i].length = buf.length;
		m_Buffers[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, m_Fd, buf.m.offset);
		if (m_Buffers[i].start == MAP_FAILED) {
			m_Buffers[i].start = nullptr;
			m_Buffers.clear();
			return false;
		}

		if (XIoctl(m_Fd, VIDIOC_QBUF, &buf) < 0) {
			m_Buffers.clear();
			return false;
		}
	}

	v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (XIoctl(m_Fd, VIDIOC_STREAMON, &type) < 0) {
		m_Buffers.clear();
		return false;
	}

	m_Streaming = true;
	return true;
}

void V4l2CameraBackend::StopStreaming()
{
	if (m_Fd >= 0 && m_Streaming) {
		v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		XIoctl(m_Fd, VIDIOC_STREAMOFF, &type);
	}
	for (MappedBuffer& buffer : m_Buffers) {
		if (buffer.start) munmap(buffer.start, buffer.length);
	}
	m_Buffers.clear();
	m_Streaming = false;

	// REQBUFS(count=0) releases the kernel's buffer allocation: VIDIOC_S_FMT fails with EBUSY while any buffers are
	// allocated, even with streaming off. Only meaningful if m_Fd is still open (also called from Close()).
	if (m_Fd >= 0) {
		v4l2_requestbuffers req{};
		req.count = 0;
		req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		req.memory = V4L2_MEMORY_MMAP;
		XIoctl(m_Fd, VIDIOC_REQBUFS, &req);
	}
}

CameraGrabResult V4l2CameraBackend::Grab(bool preferGray)
{
	CameraGrabResult result;
	if (m_Fd < 0 || !m_Streaming) return result;

	pollfd pfd{};
	pfd.fd = m_Fd;
	pfd.events = POLLIN;
	int pollResult = poll(&pfd, 1, POLL_TIMEOUT_MS);
	if (pollResult <= 0) return result; // timeout or error - not fatal, just no frame this cycle

	v4l2_buffer buf{};
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	if (XIoctl(m_Fd, VIDIOC_DQBUF, &buf) < 0) return result;

	// Drain to the newest frame: DQBUF returns the oldest queued buffer, so requeue each stale one and dequeue again
	// until the driver has nothing ready; only the final buffer is decoded/timestamped/published.
	for (;;) {
		pollfd peek{};
		peek.fd = m_Fd;
		peek.events = POLLIN;
		if (poll(&peek, 1, 0) <= 0 || !(peek.revents & POLLIN)) break; // caught up
		if (XIoctl(m_Fd, VIDIOC_QBUF, &buf) < 0) break; // give the stale one back to the driver
		v4l2_buffer next{};
		next.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		next.memory = V4L2_MEMORY_MMAP;
		if (XIoctl(m_Fd, VIDIOC_DQBUF, &next) < 0) break;
		buf = next;
	}

	// Stamped from buf.timestamp (driver's CLOCK_MONOTONIC capture instant) when populated, converted to the NowUs() epoch via a
	// one-time offset (steady_clock is CLOCK_MONOTONIC on Linux); a zero timestamp falls back to NowUs() taken after DQBUF.
	if (buf.timestamp.tv_sec != 0 || buf.timestamp.tv_usec != 0) {
		if (!m_MonotonicToWallOffsetUs.has_value()) {
			int64_t monotonicNowUs = std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			m_MonotonicToWallOffsetUs = static_cast<int64_t>(SourceResult::NowUs()) - monotonicNowUs;
		}
		int64_t bufTimestampUs = static_cast<int64_t>(buf.timestamp.tv_sec) * 1000000 + buf.timestamp.tv_usec;
		result.captureTimeUs = static_cast<uint64_t>(bufTimestampUs + m_MonotonicToWallOffsetUs.value());
	} else {
		result.captureTimeUs = SourceResult::NowUs();
	}

	v4l2_format currentFmt{};
	currentFmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	XIoctl(m_Fd, VIDIOC_G_FMT, &currentFmt);
	uint32_t fourcc = currentFmt.fmt.pix.pixelformat;

	const uint8_t* data = static_cast<const uint8_t*>(m_Buffers[buf.index].start);
	// bytesused is mandatory per spec, but a 0 from a sloppy driver falls back to the whole mapped buffer
	size_t bytesUsed = buf.bytesused > 0 ? buf.bytesused : m_Buffers[buf.index].length;

	// Every branch writes into a FramePool buffer sized to the negotiated mode (CV_8UC3 BGR, CV_8UC1 for mono), acquired before
	// decoding so Mat::create() writes in place; a frame of a different size gets a one-off unpooled allocation.
	int width = currentFmt.fmt.pix.width, height = currentFmt.fmt.pix.height;
	// bytesperline can exceed width * bytes-per-pixel (row padding, common on MIPI/ISP drivers);
	// 0 means "not reported", i.e. tightly packed
	size_t stride = currentFmt.fmt.pix.bytesperline;
	auto rawFits = [&](size_t minStride) {
		if (stride == 0) stride = minStride;
		return stride >= minStride && height > 0 && bytesUsed >= stride * (height - 1) + minStride;
	};
	auto unpackMono = [&](size_t minStride, void (*unpack)(const uint8_t*, int, int, size_t, cv::Mat&)) {
		// a short buffer (truncated frame) fails this frame rather than reading past the mapping
		if (!rawFits(minStride)) return;
		result.frame = FramePool::Instance().Acquire(height, width, CV_8UC1, result.poolOwner);
		unpack(data, width, height, stride, result.frame);
		result.format = FrameFormat::GRAY8;
		result.success = true;
	};
	if (fourcc == V4L2_PIX_FMT_MJPEG || fourcc == V4L2_PIX_FMT_JPEG) {
		cv::Mat jpegView(1, static_cast<int>(bytesUsed), CV_8UC1, const_cast<uint8_t*>(data));
		// imdecode's 3-arg overload reuses *dst in place when already the right size/type; its return value is the authoritative Mat.
		//
		// preferGray decodes straight to grayscale (IMREAD_GRAYSCALE skips chroma upsampling and colour conversion in
		// libjpeg-turbo) when nothing downstream needs colour (see ICameraBackend::Grab).
		//
		// Acquire happens before either decode path: MppJpegDecoder never touches FramePool, so the software fallback
		// reuses the same pool-owned buffer if hardware decode fails.
		if (preferGray) {
			result.frame = FramePool::Instance().Acquire(height, width, CV_8UC1, result.poolOwner);
			result.format = FrameFormat::GRAY8;
		} else {
			result.frame = FramePool::Instance().Acquire(height, width, CV_8UC3, result.poolOwner);
		}

#ifdef LUMEN_WITH_MPP_JPEG
		// hardware decode via the RK3588's JPEG VPU first; falls through to the software path below on any failure
		// (see MppJpegDecoder's contract)
		if (m_MppJpegDecoder.Decode(data, bytesUsed, width, height, preferGray, result.frame)) {
			result.success = true;
		} else
#endif
		{
			result.frame = cv::imdecode(jpegView, preferGray ? cv::IMREAD_GRAYSCALE : cv::IMREAD_COLOR, &result.frame);
			result.success = !result.frame.empty();
		}
	} else if (fourcc == V4L2_PIX_FMT_YUYV) {
		if (rawFits(static_cast<size_t>(width) * 2)) {
			cv::Mat yuyv(height, width, CV_8UC2, const_cast<uint8_t*>(data), stride);
			if (preferGray) {
				result.frame = FramePool::Instance().Acquire(height, width, CV_8UC1, result.poolOwner);
				cv::cvtColor(yuyv, result.frame, cv::COLOR_YUV2GRAY_YUYV);
				result.format = FrameFormat::GRAY8;
			} else {
				result.frame = FramePool::Instance().Acquire(height, width, CV_8UC3, result.poolOwner);
				cv::cvtColor(yuyv, result.frame, cv::COLOR_YUV2BGR_YUYV);
			}
			result.success = true;
		}
	} else if (fourcc == V4L2_PIX_FMT_BGR24 || fourcc == V4L2_PIX_FMT_RGB24) {
		if (rawFits(static_cast<size_t>(width) * 3)) {
			cv::Mat packed(height, width, CV_8UC3, const_cast<uint8_t*>(data), stride);
			result.frame = FramePool::Instance().Acquire(height, width, CV_8UC3, result.poolOwner);
			if (fourcc == V4L2_PIX_FMT_BGR24) packed.copyTo(result.frame);
			else cv::cvtColor(packed, result.frame, cv::COLOR_RGB2BGR);
			result.success = true;
		}
	} else if (fourcc == V4L2_PIX_FMT_NV12) {
		// single-planar NV12: the Y plane (height rows) directly followed by interleaved UV
		// (height/2 rows), both at the same stride - so one (height*3/2)-row Mat spans it all
		size_t rowStride = stride == 0 ? static_cast<size_t>(width) : stride;
		if (height % 2 == 0 && rowStride >= static_cast<size_t>(width) && bytesUsed >= rowStride * (height * 3 / 2 - 1) + width) {
			cv::Mat nv12(height * 3 / 2, width, CV_8UC1, const_cast<uint8_t*>(data), rowStride);
			result.frame = FramePool::Instance().Acquire(height, width, CV_8UC3, result.poolOwner);
			cv::cvtColor(nv12, result.frame, cv::COLOR_YUV2BGR_NV12);
			result.success = true;
		}
	} else if (fourcc == V4L2_PIX_FMT_GREY) {
		// passed through as GRAY8, not expanded to BGR
		unpackMono(static_cast<size_t>(width), PixelUnpack::Gray8Copy);
	} else if (fourcc == V4L2_PIX_FMT_Y10) {
		unpackMono(PixelUnpack::MinStrideY10(width), PixelUnpack::Y10ToGray8);
	} else if (fourcc == V4L2_PIX_FMT_Y16) {
		unpackMono(PixelUnpack::MinStrideY10(width), PixelUnpack::Y16ToGray8);
	} else if (fourcc == V4L2_PIX_FMT_Y10P) {
		unpackMono(PixelUnpack::MinStrideY10Packed(width), PixelUnpack::Y10PToGray8);
	} else if (fourcc == V4L2_PIX_FMT_Y10BPACK) {
		unpackMono(PixelUnpack::MinStrideY10Packed(width), PixelUnpack::Y10BPackToGray8);
	}
	// anything else: no decode path - result.success stays false for this frame rather than
	// handing back garbage reinterpreted as pixels (EnumerateModes never offers such a format).

	// requeue the same buffer regardless of decode outcome - a bad frame still has to go back to
	// the kernel or the buffer pool starves after BUFFER_COUNT failures.
	XIoctl(m_Fd, VIDIOC_QBUF, &buf);

	return result;
}

std::vector<CameraMode> V4l2CameraBackend::EnumerateModes()
{
	std::vector<CameraMode> modes;
	if (m_Fd < 0) return modes;

	for (unsigned int fmtIndex = 0; ; fmtIndex++) {
		v4l2_fmtdesc fmtDesc{};
		fmtDesc.index = fmtIndex;
		fmtDesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (XIoctl(m_Fd, VIDIOC_ENUM_FMT, &fmtDesc) < 0) break;

		std::optional<FrameFormat> format = FourCcToFrameFormat(fmtDesc.pixelformat);
		if (!format.has_value()) continue; // nothing Grab() could decode

		auto addModesForSize = [&](uint32_t width, uint32_t height) {
			for (unsigned int ivalIndex = 0; ; ivalIndex++) {
				v4l2_frmivalenum frmIval{};
				frmIval.index = ivalIndex;
				frmIval.pixel_format = fmtDesc.pixelformat;
				frmIval.width = width;
				frmIval.height = height;
				if (XIoctl(m_Fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmIval) < 0) {
					// a driver with no interval enumeration at all still has the size itself
					if (ivalIndex == 0) modes.push_back(CameraMode{ static_cast<int>(width), static_cast<int>(height), 0.0, format.value() });
					break;
				}

				if (frmIval.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
					CameraMode mode;
					mode.width = width;
					mode.height = height;
					mode.fps = frmIval.discrete.numerator > 0
						? static_cast<double>(frmIval.discrete.denominator) / frmIval.discrete.numerator
						: 0.0;
					mode.pixelFormat = format.value();
					modes.push_back(mode);
					continue;
				}

				// STEPWISE/CONTINUOUS interval range (reported once, at index 0): offer its fastest rate plus 30fps when covered
				const v4l2_fract& fastest = frmIval.stepwise.min;
				const v4l2_fract& slowest = frmIval.stepwise.max;
				double maxFps = fastest.numerator > 0 ? static_cast<double>(fastest.denominator) / fastest.numerator : 0.0;
				double minFps = slowest.numerator > 0 ? static_cast<double>(slowest.denominator) / slowest.numerator : 0.0;
				modes.push_back(CameraMode{ static_cast<int>(width), static_cast<int>(height), maxFps, format.value() });
				if (maxFps > 30.0 && minFps <= 30.0) {
					modes.push_back(CameraMode{ static_cast<int>(width), static_cast<int>(height), 30.0, format.value() });
				}
				break;
			}
		};

		for (unsigned int sizeIndex = 0; ; sizeIndex++) {
			v4l2_frmsizeenum frmSize{};
			frmSize.index = sizeIndex;
			frmSize.pixel_format = fmtDesc.pixelformat;
			if (XIoctl(m_Fd, VIDIOC_ENUM_FRAMESIZES, &frmSize) < 0) break;

			if (frmSize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
				addModesForSize(frmSize.discrete.width, frmSize.discrete.height);
				continue;
			}

			// STEPWISE/CONTINUOUS (reported once, at index 0): offer the common resolutions the range admits, plus its
			// maximum (the sensor's full native size)
			const v4l2_frmsize_stepwise& range = frmSize.stepwise;
			for (const auto& [width, height] : kCommonFrameSizes) {
				if (FitsStepwise(range, width, height) && !(width == range.max_width && height == range.max_height)) {
					addModesForSize(width, height);
				}
			}
			addModesForSize(range.max_width, range.max_height);
			break;
		}
	}

	return modes;
}

bool V4l2CameraBackend::SetMode(const CameraMode& mode)
{
	if (m_Fd < 0) return false;

	bool wasStreaming = m_Streaming;
	StopStreaming();

	bool applied = ApplyFormat(mode.width, mode.height, FrameFormatToFourCc(mode.pixelFormat));

	if (mode.fps > 0.0) {
		v4l2_streamparm parm{};
		parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		parm.parm.capture.timeperframe.numerator = 1;
		parm.parm.capture.timeperframe.denominator = static_cast<uint32_t>(mode.fps);
		// not all devices support S_PARM (frame rate control) - failure here doesn't invalidate
		// the resolution/format change ApplyFormat already made.
		XIoctl(m_Fd, VIDIOC_S_PARM, &parm);
	}

	m_RequestedMode = mode;

	if (wasStreaming) return StartStreaming() && applied;
	return applied;
}

CameraMode V4l2CameraBackend::GetCurrentMode() const
{
	CameraMode mode;
	if (m_Fd < 0) return mode;

	v4l2_format fmt{};
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (XIoctl(m_Fd, VIDIOC_G_FMT, const_cast<v4l2_format*>(&fmt)) < 0) return mode;

	mode.width = fmt.fmt.pix.width;
	mode.height = fmt.fmt.pix.height;
	// a current format Grab() can't decode can only be set outside this process; report it as MJPEG rather than failing the query
	mode.pixelFormat = FourCcToFrameFormat(fmt.fmt.pix.pixelformat).value_or(FrameFormat::MJPEG);

	v4l2_streamparm parm{};
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (XIoctl(m_Fd, VIDIOC_G_PARM, const_cast<v4l2_streamparm*>(&parm)) >= 0 &&
		parm.parm.capture.timeperframe.numerator > 0) {
		mode.fps = static_cast<double>(parm.parm.capture.timeperframe.denominator) /
			parm.parm.capture.timeperframe.numerator;
	}

	if (m_RequestedMode.has_value()) {
		const CameraMode& requested = m_RequestedMode.value();
		mode.isNative = requested.width == mode.width &&
			requested.height == mode.height &&
			requested.pixelFormat == mode.pixelFormat;
	}

	return mode;
}

uint32_t V4l2CameraBackend::FindControl(std::initializer_list<uint32_t> candidates) const
{
	if (m_Fd < 0) return 0;
	for (uint32_t cid : candidates) {
		v4l2_queryctrl query{};
		query.id = cid;
		if (XIoctl(m_Fd, VIDIOC_QUERYCTRL, &query) >= 0 && !(query.flags & V4L2_CTRL_FLAG_DISABLED)) return cid;
	}
	return 0;
}

CameraControlRange V4l2CameraBackend::QueryControlRange(uint32_t cid) const
{
	CameraControlRange range;
	if (m_Fd < 0 || cid == 0) return range;

	v4l2_queryctrl query{};
	query.id = cid;
	if (XIoctl(m_Fd, VIDIOC_QUERYCTRL, &query) < 0) return range;
	range.supported = true;
	range.minimum = query.minimum;
	range.maximum = query.maximum;
	range.step = query.step > 0 ? query.step : 1;
	range.defaultValue = query.default_value;

	v4l2_control ctrl{};
	ctrl.id = cid;
	range.value = XIoctl(m_Fd, VIDIOC_G_CTRL, &ctrl) >= 0 ? ctrl.value : query.default_value;
	return range;
}

// UVC webcams implement EXPOSURE_ABSOLUTE (100us units); raw-sensor drivers - Arducam's MIPI
// OV9281/OV9782 modules among them - implement only EXPOSURE (sensor-specific units, typically
// lines). Whichever exists is "the" exposure control; GetExposureRange reports the same one.
bool V4l2CameraBackend::SetExposure(int exposureAbsolute)
{
	uint32_t cid = FindControl({ V4L2_CID_EXPOSURE_ABSOLUTE, V4L2_CID_EXPOSURE });
	if (cid == 0) return false;
	v4l2_control ctrl{};
	ctrl.id = cid;
	ctrl.value = exposureAbsolute;
	return XIoctl(m_Fd, VIDIOC_S_CTRL, &ctrl) >= 0;
}

bool V4l2CameraBackend::SetAutoExposure(bool enabled)
{
	if (m_Fd < 0) return false;
	v4l2_control ctrl{};
	ctrl.id = V4L2_CID_EXPOSURE_AUTO;
	if (!enabled) {
		ctrl.value = V4L2_EXPOSURE_MANUAL;
		return XIoctl(m_Fd, VIDIOC_S_CTRL, &ctrl) >= 0;
	}
	// UVC convention is APERTURE_PRIORITY (most webcams implement only it and MANUAL of the four
	// standard values); some non-UVC drivers accept only plain AUTO instead - try both
	ctrl.value = V4L2_EXPOSURE_APERTURE_PRIORITY;
	if (XIoctl(m_Fd, VIDIOC_S_CTRL, &ctrl) >= 0) return true;
	ctrl.value = V4L2_EXPOSURE_AUTO;
	return XIoctl(m_Fd, VIDIOC_S_CTRL, &ctrl) >= 0;
}

// same UVC-vs-raw-sensor split as SetExposure: GAIN on webcams, ANALOGUE_GAIN on sensor drivers
bool V4l2CameraBackend::SetGain(int gain)
{
	uint32_t cid = FindControl({ V4L2_CID_GAIN, V4L2_CID_ANALOGUE_GAIN });
	if (cid == 0) return false;
	v4l2_control ctrl{};
	ctrl.id = cid;
	ctrl.value = gain;
	return XIoctl(m_Fd, VIDIOC_S_CTRL, &ctrl) >= 0;
}

CameraControlRange V4l2CameraBackend::GetExposureRange()
{
	return QueryControlRange(FindControl({ V4L2_CID_EXPOSURE_ABSOLUTE, V4L2_CID_EXPOSURE }));
}

CameraControlRange V4l2CameraBackend::GetGainRange()
{
	return QueryControlRange(FindControl({ V4L2_CID_GAIN, V4L2_CID_ANALOGUE_GAIN }));
}
