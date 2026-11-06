#include "TagOverlay.h"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <vector>

void DrawTagPose(cv::Mat& image, const double R[9], const double t[3], double tagSize,
	const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs)
{
	const double h = tagSize / 2.0;
	const double axisLength = tagSize / 2.0;
	// the box stands on the tag and rises towards the camera, which is -Z of the tag frame
	const std::vector<cv::Point3d> points = {
		{ -h, h, 0 }, { h, h, 0 }, { h, -h, 0 }, { -h, -h, 0 },
		{ -h, h, -tagSize }, { h, h, -tagSize }, { h, -h, -tagSize }, { -h, -h, -tagSize },
		{ 0, 0, 0 }, { axisLength, 0, 0 }, { 0, axisLength, 0 }, { 0, 0, -axisLength },
	};

	// the points in the camera's frame; a segment is clipped at a near plane just in front of the lens and the part behind it is dropped. The box
	// is a whole tag size tall, so on a close, tilted tag its far face crosses the camera's plane: that must not take the visible part of the
	// box (and the axes) with it.
	std::vector<cv::Point3d> camera;
	camera.reserve(points.size());
	for (const cv::Point3d& point : points) {
		camera.emplace_back(R[0] * point.x + R[1] * point.y + R[2] * point.z + t[0],
			R[3] * point.x + R[4] * point.y + R[5] * point.z + t[1],
			R[6] * point.x + R[7] * point.y + R[8] * point.z + t[2]);
	}
	const double nearPlane = tagSize * 0.01;

	auto line = [&](int from, int to, const cv::Scalar& colour, int thickness) {
		cv::Point3d a = camera[from], b = camera[to];
		if (a.z < nearPlane && b.z < nearPlane) return;
		// move the endpoint that is behind the near plane onto it
		if (a.z < nearPlane) a = a + (b - a) * ((nearPlane - a.z) / (b.z - a.z));
		else if (b.z < nearPlane) b = b + (a - b) * ((nearPlane - b.z) / (a.z - b.z));
		std::vector<cv::Point2d> projected;
		cv::projectPoints(std::vector<cv::Point3d>{ a, b }, cv::Vec3d(0, 0, 0), cv::Vec3d(0, 0, 0), cameraMatrix, distCoeffs, projected);
		// a point just in front of the lens lands far off the image: keep it in the integer range cv::line clips in
		auto at = [](const cv::Point2d& p) {
			return cv::Point(cvRound(std::max(-1e6, std::min(1e6, p.x))), cvRound(std::max(-1e6, std::min(1e6, p.y))));
		};
		cv::line(image, at(projected[0]), at(projected[1]), colour, thickness, cv::LINE_AA);
	};

	const cv::Scalar boxColour(0x00, 0xff, 0x00);
	for (int i = 0; i < 4; i++) {
		line(i, 4 + i, boxColour, 2);
		line(4 + i, 4 + (i + 1) % 4, boxColour, 2);
	}
	line(8, 9, cv::Scalar(0, 0, 0xff), 3);   // X: red
	line(8, 10, cv::Scalar(0, 0xff, 0), 3);  // Y: green
	line(8, 11, cv::Scalar(0xff, 0, 0), 3);  // Z: blue
}
