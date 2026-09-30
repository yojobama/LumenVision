#pragma once
#include "CoordinateFrames.h"
#include <opencv2/core.hpp>
#include <vector>

// Perspective-n-point for a robot that stays flat on the floor: only its field position (x, y) and heading (yaw) are unknown, while
// its height and roll/pitch are fixed at zero. With a known camera mount this needs far less data than a free six-degree-of-freedom
// solve (one tag is enough) and cannot drift off the floor.
struct ConstrainedPnpInput {
	// tag corners in the field frame and the pixels they were detected at (lens distortion still present), paired by index
	std::vector<cv::Point3d> fieldPoints;
	std::vector<cv::Point2d> imagePoints;
	cv::Mat cameraMatrix;
	cv::Mat distCoeffs;
	// the camera's pose in the robot frame, both in WPILib axes (camera X forward / Y left / Z up)
	frames::Pose3 robotToCamera;
	// starting guess: robot x, y in metres and yaw in radians
	double seedX = 0.0;
	double seedY = 0.0;
	double seedYaw = 0.0;
};

struct ConstrainedPnpResult {
	bool ok = false;
	double x = 0.0;
	double y = 0.0;
	double yaw = 0.0; // radians
	double reprojErrPixels = 0.0;
	int iterations = 0;
};

// Levenberg-Marquardt over (x, y, yaw) minimising the pixel reprojection error of the field points.
ConstrainedPnpResult SolveConstrainedPnp(const ConstrainedPnpInput& input);
