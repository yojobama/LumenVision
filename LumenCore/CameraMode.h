#pragma once
#include <string>
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
