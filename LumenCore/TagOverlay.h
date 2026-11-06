#pragma once
#include <opencv2/core.hpp>

// Draws a detected tag's pose on a BGR frame the way PhotonVision does: the tag's 3D box (extruded towards the camera) and its
// X (red), Y (green) and Z (blue) axes. R is the row-major 3x3 rotation and t the translation from libapriltag's pose (tag frame:
// X right, Y down, Z into the tag), in the camera's pixels described by cameraMatrix and distCoeffs (empty = no distortion).
// Every line is clipped at a near plane in front of the lens, so a box that reaches behind the camera (a close, tilted tag) still shows the
// part in front; a tag wholly behind the camera draws nothing.
void DrawTagPose(cv::Mat& image, const double R[9], const double t[3], double tagSize,
	const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs);
