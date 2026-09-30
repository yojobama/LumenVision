#pragma once
#include <string>
#include <vector>
#include "FrameFormat.h"

// A capture mode a camera device can produce; isNative reports whether a request was honoured.
// V4L2 and Media Foundation silently substitute the nearest mode, so isNative is only meaningful on a
// mode from GetCurrentMode() after SetMode(), not one from EnumerateModes().
struct CameraMode
{
	int width = 0;
	int height = 0;
	// frames per second, not an interval (a 1/30s frame interval becomes 30.0)
	double fps = 0.0;
	// the device's wire format (usually MJPEG or YUYV); V4l2CameraBackend decodes it to BGR24, so
	// delivered Frames are not tagged with this
	FrameFormat pixelFormat = FrameFormat::MJPEG;
	// true only on a CameraMode returned by GetCurrentMode() when the device's actual settings,
	// read back after SetMode(), exactly match what was requested.
	bool isNative = false;
};

// A camera control's range as reported by the device (V4L2 VIDIOC_QUERYCTRL), so the UI can bound
// its inputs. supported == false (all other fields meaningless) when the control doesn't exist.
struct CameraControlRange
{
	bool supported = false;
	int minimum = 0;
	int maximum = 0;
	int step = 1;
	int defaultValue = 0;
	// the control's value right now (VIDIOC_G_CTRL), so the UI starts from what's applied
	int value = 0;
};

// One control a camera device exposes (V4L2 VIDIOC_QUERYCTRL), for a generic controls UI. `id` is the backend's own identifier (the
// V4L2 control id) and is what SetControl takes.
enum CameraControlKind
{
	CAMERA_CONTROL_INTEGER = 0,
	CAMERA_CONTROL_BOOLEAN = 1,
	// choose one of `menuLabels` (the value is the index into the device's menu; `menuValues` holds each entry's value)
	CAMERA_CONTROL_MENU = 2,
	// writing any value triggers it (V4L2 button controls, e.g. "restore defaults")
	CAMERA_CONTROL_BUTTON = 3,
};

struct CameraControlInfo
{
	int id = 0;
	std::string name;
	CameraControlKind kind = CAMERA_CONTROL_INTEGER;
	int minimum = 0;
	int maximum = 0;
	int step = 1;
	int defaultValue = 0;
	int value = 0;
	// the control cannot be written at all (read-only) or is unavailable in the current configuration (e.g. manual exposure time
	// while auto exposure is on)
	bool readOnly = false;
	bool inactive = false;
	std::vector<std::string> menuLabels;
	std::vector<int> menuValues;
};
