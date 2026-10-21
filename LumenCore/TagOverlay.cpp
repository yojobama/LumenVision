#include "TagOverlay.h"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
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

	cv::Mat rotation(3, 3, CV_64F);
	for (int i = 0; i < 9; i++) rotation.at<double>(i / 3, i % 3) = R[i];
	cv::Mat rvec;
	cv::Rodrigues(rotation, rvec);
	const cv::Mat tvec = (cv::Mat_<double>(3, 1) << t[0], t[1], t[2]);

	for (const cv::Point3d& point : points) {
		const double z = R[6] * point.x + R[7] * point.y + R[8] * point.z + t[2];
		if (z <= 0.0) return;
	}

	std::vector<cv::Point2d> projected;
	cv::projectPoints(points, rvec, tvec, cameraMatrix, distCoeffs, projected);
	auto at = [&](int i) { return cv::Point(cvRound(projected[i].x), cvRound(projected[i].y)); };

	const cv::Scalar boxColour(0x00, 0xff, 0x00);
	for (int i = 0; i < 4; i++) {
		cv::line(image, at(i), at(4 + i), boxColour, 2, cv::LINE_AA);
		cv::line(image, at(4 + i), at(4 + (i + 1) % 4), boxColour, 2, cv::LINE_AA);
	}
	cv::line(image, at(8), at(9), cv::Scalar(0, 0, 0xff), 3, cv::LINE_AA);   // X: red
	cv::line(image, at(8), at(10), cv::Scalar(0, 0xff, 0), 3, cv::LINE_AA);  // Y: green
	cv::line(image, at(8), at(11), cv::Scalar(0xff, 0, 0), 3, cv::LINE_AA);  // Z: blue
}
