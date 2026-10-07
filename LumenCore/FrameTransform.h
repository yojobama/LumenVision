#pragma once
#include "CameraCalibrationResult.h"
#ifndef SWIG
#include <opencv2/core.hpp>
#endif

// A fixed reshaping of a camera's frames: crop, then rotate clockwise, then flip. The crop rectangle is in the camera's own pixels; a
// zero cropWidth or cropHeight means no crop.
struct FrameTransform
{
	// clockwise degrees: 0, 90, 180 or 270
	int rotation = 0;
	bool flipHorizontal = false;
	bool flipVertical = false;
	int cropX = 0;
	int cropY = 0;
	int cropWidth = 0;
	int cropHeight = 0;

	bool IsIdentity() const { return rotation == 0 && !flipHorizontal && !flipVertical && cropWidth <= 0 && cropHeight <= 0; }

#ifndef SWIG
	// the crop clipped to an image of this size, or the whole image when there is none
	cv::Rect EffectiveCrop(cv::Size input) const;
	// the size of a transformed image
	cv::Size OutputSize(cv::Size input) const;
	// Applies the transform. An unchanged result is `input` itself (viewOfInput = true); anything else is a new, contiguous image.
	cv::Mat Apply(const cv::Mat& input, bool& viewOfInput) const;
#endif
};

// The calibration of the frames a camera produces once `transform` is applied, given its calibration at the camera's own resolution:
// the principal point and focal lengths follow the pixels, and the tangential distortion terms follow the axes (radial terms are unchanged
// by rotating or mirroring about the principal point).
CameraCalibrationResult TransformCalibration(const CameraCalibrationResult& calibration, const FrameTransform& transform);
