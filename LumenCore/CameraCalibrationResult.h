#pragma once

#include <apriltag/apriltag.h>
#include <vector>

class CameraCalibrationResult {
public:
    // Camera intrinsic parameters
    double fx; // Focal length in x (pixels)
    double fy; // Focal length in y (pixels)
    double cx; // Principal point x (pixels)
    double cy; // Principal point y (pixels)
	double rms; // Root Mean Square error of the calibration, <0.5 is good, <1.0 is acceptable, >1.0 is bad

    // Distortion coefficients in OpenCV's (k1, k2, p1, p2, k3[, k4, k5, k6]) order. Empty means no
    // calibration was performed; consumers must check it before undistorting.
    std::vector<double> distCoeffs;

    // resolution the calibration was performed at; a result is only valid for a source
    // producing frames at this exact size
    int imageWidth;
    int imageHeight;

    CameraCalibrationResult()
        : fx(0.0), fy(0.0), cx(0.0), cy(0.0), rms(0.0), imageWidth(0), imageHeight(0) {
    }

    CameraCalibrationResult(double fx, double fy, double cx, double cy, double rms,
        std::vector<double> distCoeffs, int imageWidth, int imageHeight)
        : fx(fx), fy(fy), cx(cx), cy(cy), rms(rms),
          distCoeffs(std::move(distCoeffs)), imageWidth(imageWidth), imageHeight(imageHeight) {
    }

    bool HasDistortion() const { return !distCoeffs.empty(); }
};
