#pragma once
#include "ICameraBackend.h"
#include "MppJpegDecoder.h"
#include <optional>
#include <vector>
#include <cstdint>
#include <initializer_list>

// A V4L2 capture backend (Linux only): mmap'd kernel buffers, poll()-gated DQBUF, explicit mode/exposure control.
//
// MJPEG/YUYV/NV12 are decoded to BGR24 in Grab() itself, since Frame has no decode path for them; mono formats
// (GREY, raw Y10/Y16/Y10P/Y10BPACK) come out as GRAY8 (see PixelUnpack.h).
class V4l2CameraBackend : public ICameraBackend
{
public:
	~V4l2CameraBackend() override;

	bool Open(const std::string& devicePath) override;
	void Close() override;
	bool IsOpened() const override;
	CameraGrabResult Grab(bool preferGray = false) override;
	std::string Name() const override { return "V4L2"; }

	std::vector<CameraMode> EnumerateModes() override;
	bool SetMode(const CameraMode& mode) override;
	CameraMode GetCurrentMode() const override;
	bool SetExposure(int exposureAbsolute) override;
	bool SetAutoExposure(bool enabled) override;
	bool SetGain(int gain) override;
	CameraControlRange GetExposureRange() override;
	CameraControlRange GetGainRange() override;
	std::vector<CameraControlInfo> EnumerateControls() override;
	bool SetControl(int id, int value) override;

private:
	struct MappedBuffer
	{
		void* start = nullptr;
		size_t length = 0;
	};

	// Requests kernel buffers, mmaps and queues them, and calls STREAMON; no-op if already streaming.
	bool StartStreaming();
	// Calls STREAMOFF and munmaps every buffer; no-op if not streaming. Keeps the device fd open
	// (SetMode() uses this to reconfigure the format).
	void StopStreaming();
	bool ApplyFormat(int width, int height, uint32_t fourcc);
	// The first of `candidates` this device implements (VIDIOC_QUERYCTRL succeeds, not disabled), or 0 if none;
	// UVC exposes EXPOSURE_ABSOLUTE/GAIN, raw sensor drivers EXPOSURE/ANALOGUE_GAIN.
	uint32_t FindControl(std::initializer_list<uint32_t> candidates) const;
	CameraControlRange QueryControlRange(uint32_t cid) const;

	int m_Fd = -1;
	std::string m_DevicePath;
	bool m_Streaming = false;
	std::vector<MappedBuffer> m_Buffers;
	// the last mode SetMode() applied; GetCurrentMode()'s isNative flag compares the device's read-back settings against it
	std::optional<CameraMode> m_RequestedMode;
	// converts a V4L2 buf.timestamp (CLOCK_MONOTONIC) to the SourceResult::NowUs() wall-clock epoch; computed once, lazily, on the first frame
	std::optional<int64_t> m_MonotonicToWallOffsetUs;
#ifdef LUMEN_WITH_MPP_JPEG
	// tried first in the MJPEG branch of Grab(); any failure falls back to software cv::imdecode
	MppJpegDecoder m_MppJpegDecoder;
#endif
};
