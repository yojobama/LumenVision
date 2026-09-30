#pragma once
#include "CoordinateFrames.h"
#include <opencv2/calib3d.hpp>
#include <cmath>
#include <vector>

// A scene described in WPILib terms: a camera pose in the field and tags facing it, with a pinhole camera to project them through.
namespace testscene {

using frames::Pose3;

inline cv::Matx33d ToMatx(const std::array<double, 9>& R) {
	return cv::Matx33d(R[0], R[1], R[2], R[3], R[4], R[5], R[6], R[7], R[8]);
}

inline std::array<double, 9> FromMatx(const cv::Matx33d& m) {
	return { m(0, 0), m(0, 1), m(0, 2), m(1, 0), m(1, 1), m(1, 2), m(2, 0), m(2, 1), m(2, 2) };
}

inline cv::Matx33d Rodrigues(double rx, double ry, double rz) {
	cv::Mat r;
	cv::Rodrigues(cv::Vec3d(rx, ry, rz), r);
	return cv::Matx33d(r.at<double>(0, 0), r.at<double>(0, 1), r.at<double>(0, 2),
		r.at<double>(1, 0), r.at<double>(1, 1), r.at<double>(1, 2),
		r.at<double>(2, 0), r.at<double>(2, 1), r.at<double>(2, 2));
}

struct Scene {
	double tagSize = 0.1651;
	cv::Matx33d cameraMatrix{ 1000, 0, 640, 0, 1000, 400, 0, 0, 1 };

	// ground truth, all in the WPILib field frame
	cv::Vec3d cameraPosition{ 2.0, 1.0, 0.6 };
	cv::Matx33d cameraRotation = Rodrigues(0.03, -0.08, 0.15); // camera X forward, Y left, Z up, expressed in field coordinates

	struct Tag {
		cv::Vec3d position;
		cv::Matx33d rotation; // the tag's WPILib frame (X out of its face) in field coordinates
	};
	std::vector<Tag> tags = {
		{ cv::Vec3d(6.0, 1.6, 0.8), Rodrigues(0, 0, CV_PI) * Rodrigues(0.04, 0.0, 0.0) },   // facing back toward the camera
		{ cv::Vec3d(6.2, 0.4, 0.3), Rodrigues(0, 0, CV_PI - 0.3) * Rodrigues(0.0, 0.05, 0.0) },
	};

	// field -> OpenCV camera (rvec/tvec as solvePnP reports them)
	void WorldToOpenCvCamera(cv::Mat& rvec, cv::Mat& tvec) const {
		// the WPILib camera axes are the OpenCV ones permuted: R_wpilib = R_opencv * T, with T mapping WPILib-frame vectors to OpenCV ones
		const cv::Matx33d T(0, -1, 0, 0, 0, -1, 1, 0, 0);
		cv::Matx33d cameraInFieldOpenCv = cameraRotation * T.t();
		cv::Matx33d worldToCamera = cameraInFieldOpenCv.t();
		cv::Rodrigues(worldToCamera, rvec);
		tvec = cv::Mat(-(worldToCamera * cameraPosition));
	}

	std::vector<cv::Point3d> FieldCorners(const Tag& tag) const {
		std::vector<cv::Point3d> out;
		for (const auto& c : frames::WpilibTagCorners(tagSize)) {
			cv::Vec3d p = tag.rotation * cv::Vec3d(c[0], c[1], c[2]) + tag.position;
			out.emplace_back(p[0], p[1], p[2]);
		}
		return out;
	}

	std::vector<cv::Point2d> Project(const std::vector<cv::Point3d>& points) const {
		cv::Mat rvec, tvec;
		WorldToOpenCvCamera(rvec, tvec);
		std::vector<cv::Point2d> pixels;
		cv::projectPoints(points, rvec, tvec, cv::Mat(cameraMatrix), cv::Mat::zeros(4, 1, CV_64F), pixels);
		return pixels;
	}
};


}
