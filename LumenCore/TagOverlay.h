#pragma once
#include <opencv2/core.hpp>

// Draws a detected tag's pose on a BGR frame the way PhotonVision does: the tag's 3D box (extruded towards the camera) and its
// X (red), Y (green) and Z (blue) axes. R is the row-major 3x3 rotation and t the translation from libapriltag's pose (tag frame:
// X right, Y down, Z into the tag), in the camera's pixels described by cameraMatrix and distCoeffs (empty = no distortion).
// Nothing is drawn when any of the box's points is behind the camera.
void DrawTagPose(cv::Mat& image, const double R[9], const double t[3], double tagSize,
	const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs);
