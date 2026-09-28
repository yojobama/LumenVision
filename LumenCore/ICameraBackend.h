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
	uint64_t captureTimeUs = 0;
	// non-null when `frame` is backed by a FramePool buffer (V4l2CameraBackend sets it; null means `frame` owns its memory).
	// CameraFrameSource::CaptureFrame must carry it into the Frame it builds, or the buffer can be recycled while still in use.
	std::shared_ptr<void> poolOwner;
	// what `frame` holds: BGR24 (CV_8UC3) or GRAY8 (CV_8UC1). Mono frames stay single-channel to the AprilTag detector;
	// sinks needing colour convert lazily via Frame::AsBgr().
	FrameFormat format = FrameFormat::BGR24;
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
	// decode straight to grayscale should. Backends may ignore this hint and always produce BGR.
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
};
