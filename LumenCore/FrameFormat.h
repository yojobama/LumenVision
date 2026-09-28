#pragma once

// Pixel formats a Frame can carry or a CameraMode can advertise. MJPEG/YUYV and the raw mono formats (Y10 onwards) are for
// CameraMode reporting only: V4l2CameraBackend decodes/unpacks them at capture (to BGR24 and GRAY8), so no Frame carries one.
//
// Ordinals are part of the wire contract (REST API and webui PIXEL_FORMAT_NAMES): append new values, never reorder.
enum class FrameFormat {
	BGR24,
	RGB24,
	GRAY8,
	NV12,
	YUYV,
	MJPEG,
	// raw mono sensor formats (e.g. Arducam OV9281); see PixelUnpack.h for layouts
	Y10,
	Y16,
	Y10P,
	Y10BPACK,
};
