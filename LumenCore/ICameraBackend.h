#pragma once
#include <opencv2/opencv.hpp>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include "CameraMode.h"

// A single captured frame plus the instant it was captured.
struct CameraGrabResult
{
	bool success = false;
	cv::Mat frame;
	// NV12 only (format == NV12): `frame` is the luma plane and this the interleaved chroma plane, both views into a buffer `poolOwner` holds
	cv::Mat nv12Uv;
	uint64_t captureTimeUs = 0;
	// non-null when `frame` is backed by a FramePool buffer (V4l2CameraBackend sets it; null means `frame` owns its memory).
	// CameraFrameSource::CaptureFrame must carry it into the Frame it builds, or the buffer can be recycled while still in use.
	std::shared_ptr<void> poolOwner;
	// what `frame` holds: BGR24 (CV_8UC3), GRAY8 (CV_8UC1) or NV12 (the luma plane, see nv12Uv). Mono frames stay single-channel to the AprilTag detector;
	// sinks needing colour convert lazily via Frame::AsBgr().
	FrameFormat format = FrameFormat::BGR24;
	// where Grab() spent its time, for the node's stage profile (0 = the backend does not measure it): waiting for the driver to hand over
	// the next frame (about one frame interval, so not a cost), and turning that frame into pixels
	double waitFrameUs = 0;
	double decodeUs = 0;
	// of decodeUs, for a hardware JPEG decode: the decoder block itself and the CPU copy/convert out of its buffer; both stay 0 when
	// the software decoder ran
	double hwDecodeUs = 0;
	double copyOutUs = 0;
};

// Implemented per platform: OpenCvCameraBackend (always available) and V4l2CameraBackend (Linux).
// CameraFrameSource owns one of these rather than a cv::VideoCapture.
class ICameraBackend
{
public:
	virtual ~ICameraBackend() = default;

	// Opens the device at devicePath. Returns false rather than throwing - the caller decides
	// whether a failed open is fatal.
	virtual bool Open(const std::string& devicePath) = 0;
	virtual void Close() = 0;
	virtual bool IsOpened() const = 0;

	// Must return within a bounded time even if the device stops producing frames (e.g. unplugged): ISource::Toggle(false)
	// blocks joining the capture thread, so a backend that blocks forever here hangs stop/delete/shutdown.
	// preferGray: true when no bound sink needs colour this cycle (see ISource::HasActiveFrameConsumer); a backend that can
	// decode straight to greyscale should. Backends may ignore this hint and always produce BGR.
	virtual CameraGrabResult Grab(bool preferGray = false) = 0;

	virtual std::string Name() const = 0;

	// Real device capabilities, queried after Open(). Empty on a backend that cannot enumerate them
	// (V4L2 uses ioctls; OpenCvCameraBackend on Windows queries Media Foundation directly).
	virtual std::vector<CameraMode> EnumerateModes() = 0;

	// Both V4L2 and Media Foundation silently substitute the nearest mode: callers MUST re-read GetCurrentMode() and check
	// isNative. This return value only reports whether the underlying call succeeded.
	virtual bool SetMode(const CameraMode& mode) = 0;
	virtual CameraMode GetCurrentMode() const = 0;

	// Exposure/gain control. exposureAbsolute is in the backend's native units (V4L2: 100us steps for EXPOSURE_ABSOLUTE, or
	// the sensor's own units, typically lines, for EXPOSURE; see GetExposureRange). Returns false if unsupported.
	virtual bool SetExposure(int exposureAbsolute) = 0;
	virtual bool SetAutoExposure(bool enabled) = 0;
	virtual bool SetGain(int gain) = 0;

	// The device's real range for the control SetExposure/SetGain drive - see CameraControlRange.
	// Default: unsupported, for a backend with no way to query it (OpenCvCameraBackend).
	virtual CameraControlRange GetExposureRange() { return {}; }
	virtual CameraControlRange GetGainRange() { return {}; }

	// Every control the device exposes, with its current value; empty on a backend that cannot enumerate them.
	virtual std::vector<CameraControlInfo> EnumerateControls() { return {}; }
	// Writes one control by CameraControlInfo::id. Returns false if it does not exist or the device rejected the value.
	virtual bool SetControl(int id, int value) { (void)id; (void)value; return false; }
};
