#pragma once

#include "CameraCalibrationResult.h"
#include <vector>

// SWIG-safe like CameraCalibrationResult.h: plain doubles and vector<double>, no cv:: types
// (StereoCalibrator.h must not reach swig.i).
class StereoCalibrationResult {
public:
	// per-eye intrinsics + distortion, from cv::stereoCalibrate (or two prior CameraCalibrator runs with CALIB_FIX_INTRINSIC)
	CameraCalibrationResult left;
	CameraCalibrationResult right;

	// right-relative-to-left extrinsics: R is row-major 3x3 (9 entries), T is 3x1 (3 entries)
	std::vector<double> R;
	std::vector<double> T;
	// essential / fundamental matrices, row-major 3x3 (9 entries each)
	std::vector<double> E;
	std::vector<double> F;

	// cv::stereoRectify output. R1/R2 are row-major 3x3 (9 entries each, rectifying rotations).
	// P1/P2 are row-major 3x4 (12 entries each, rectified projection matrices). Q is row-major
	// 4x4 (16 entries, disparity-to-depth reprojection matrix).
	std::vector<double> R1;
	std::vector<double> R2;
	std::vector<double> P1;
	std::vector<double> P2;
	std::vector<double> Q;

	double stereoRms = 0.0;       // cv::stereoCalibrate's own return value
	// mean |y_left - y_right| over the saved corner sets after rectification; predicts codec-stereo validity better
	// than stereoRms (blocks are gated on |dy|). Gate real use at < 0.5px.
	double epipolarRms = 0.0;
	double baselineMeters = 0.0;  // norm(T)
	double rectifiedFx = 0.0;     // P1[0] - the focal length codec-stereo's disparity->depth math needs
	double rectifiedCx = 0.0;
	double rectifiedCy = 0.0;
	int imageWidth = 0;
	int imageHeight = 0;

	// cv::stereoRectify's validPixROI1/2: the sub-rectangle of the rectified image that is pixel-valid;
	// StereoDepthNode crops to their intersection
	int roiLeftX = 0, roiLeftY = 0, roiLeftW = 0, roiLeftH = 0;
	int roiRightX = 0, roiRightY = 0, roiRightW = 0, roiRightH = 0;

	StereoCalibrationResult() = default;

	bool IsValid() const { return !Q.empty() && baselineMeters > 0.0; }
};
