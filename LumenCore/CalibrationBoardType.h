#pragma once

// Split from CameraCalibrator.h so Manager.h can expose it via SWIG without the heavy charuco header.
// Plain (unscoped) enum, like the other SWIG-exposed enums: SWIG wraps a scoped enum as an opaque handle.
enum CalibrationBoardType {
	BOARD_CHECKERBOARD,
	BOARD_CHARUCO
};
