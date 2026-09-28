#include <catch2/catch_test_macros.hpp>
#include "ApriltagDetector.h"
#include "AprilTagFieldLayout.h"
#include <opencv2/calib3d.hpp>
#include <fstream>
#include <cmath>

// SolveMultiTagPnP against synthetic correspondences.

namespace {
	// Local corner order matches det->p[0..3]; local +Z is the tag normal.
	std::vector<cv::Point3d> LocalCorners(double tagSize) {
		double h = tagSize / 2.0;
		return { {-h, h, 0}, {h, h, 0}, {h, -h, 0}, {-h, -h, 0} };
	}

	std::vector<cv::Point3d> FieldCorners(const AprilTagFieldPose& pose, double tagSize) {
		std::vector<cv::Point3d> out;
		cv::Vec3d t(pose.translation.x, pose.translation.y, pose.translation.z);
		for (const auto& local : LocalCorners(tagSize)) {
			cv::Vec3d p = pose.rotation * cv::Vec3d(local.x, local.y, local.z) + t;
			out.emplace_back(p[0], p[1], p[2]);
		}
		return out;
	}
}

TEST_CASE("AprilTagFieldLayout loads WPILib-format JSON and converts quaternions correctly", "[multitag]") {
	std::string path = "test_field_layout.json";
	{
		std::ofstream f(path);
		f << R"({
			"tags": [
				{"ID": 1, "pose": {"translation": {"x": 1.0, "y": 2.0, "z": 3.0},
				                   "rotation": {"quaternion": {"W": 1, "X": 0, "Y": 0, "Z": 0}}}},
				{"ID": 2, "pose": {"translation": {"x": 5.0, "y": 0.0, "z": 1.0},
				                   "rotation": {"quaternion": {"W": 0.7071067811865476, "X": 0, "Y": 0, "Z": 0.7071067811865476}}}}
			]
		})";
	}

	AprilTagFieldLayout layout;
	REQUIRE(layout.LoadFromFile(path));
	REQUIRE(layout.size() == 2);

	AprilTagFieldPose pose1;
	REQUIRE(layout.TryGetTagPose(1, pose1));
	REQUIRE(pose1.translation.x == 1.0);
	REQUIRE(pose1.translation.y == 2.0);
	REQUIRE(pose1.translation.z == 3.0);
	// identity quaternion (W=1) -> identity rotation matrix
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++)
			REQUIRE(std::abs(pose1.rotation(r, c) - (r == c ? 1.0 : 0.0)) < 1e-9);

	AprilTagFieldPose pose2;
	REQUIRE(layout.TryGetTagPose(2, pose2));
	// 90deg about Z maps local X (1,0,0) to (0,1,0).
	cv::Vec3d rotatedX = pose2.rotation * cv::Vec3d(1, 0, 0);
	REQUIRE(std::abs(rotatedX[0] - 0.0) < 1e-6);
	REQUIRE(std::abs(rotatedX[1] - 1.0) < 1e-6);
	REQUIRE(std::abs(rotatedX[2] - 0.0) < 1e-6);

	REQUIRE_FALSE(layout.TryGetTagPose(99, pose1)); // unknown id

	std::remove(path.c_str());
}

TEST_CASE("SolveMultiTagPnP recovers a known camera pose from two tags at different field poses", "[multitag]") {
	const double tagSize = 0.1651;
	const double fx = 900, fy = 900, cx = 640, cy = 480;
	cv::Mat cameraMatrix = (cv::Mat_<double>(3, 3) << fx, 0, cx, 0, fy, cy, 0, 0, 1);
	cv::Mat distCoeffs = cv::Mat::zeros(4, 1, CV_64F);

	// Two tags at different positions and orientations.
	AprilTagFieldPose tag1;
	tag1.translation = { 3.0, 1.0, 0.5 };
	tag1.rotation = cv::Matx33d::eye();

	AprilTagFieldPose tag2;
	tag2.translation = { 3.0, -1.0, 0.8 };
	// tag2 rotated 20deg about its own Y axis relative to tag1
	cv::Mat rvecTag2 = (cv::Mat_<double>(3, 1) << 0, 0.349, 0);
	cv::Mat rmatTag2;
	cv::Rodrigues(rvecTag2, rmatTag2);
	tag2.rotation = cv::Matx33d(
		rmatTag2.at<double>(0,0), rmatTag2.at<double>(0,1), rmatTag2.at<double>(0,2),
		rmatTag2.at<double>(1,0), rmatTag2.at<double>(1,1), rmatTag2.at<double>(1,2),
		rmatTag2.at<double>(2,0), rmatTag2.at<double>(2,1), rmatTag2.at<double>(2,2));

	// ground truth: the camera's own pose IN FIELD frame
	cv::Vec3d cameraPositionInField(0.2, 0.1, 0.6);
	cv::Mat rvecCameraInField = (cv::Mat_<double>(3, 1) << 0.05, -0.1, 0.02);
	cv::Mat rmatCameraInField;
	cv::Rodrigues(rvecCameraInField, rmatCameraInField);

	// projectPoints needs world-to-camera (rvec,tvec), the inverse of camera-in-field
	cv::Mat rmatWorldToCamera = rmatCameraInField.t();
	cv::Mat rvecWorldToCamera;
	cv::Rodrigues(rmatWorldToCamera, rvecWorldToCamera);
	cv::Mat tvecWorldToCamera = -rmatWorldToCamera * cv::Mat(cameraPositionInField);

	std::vector<cv::Point3d> objectPoints;
	std::vector<cv::Point2d> imagePoints;
	for (const auto& tagPose : { tag1, tag2 }) {
		auto fieldCorners = FieldCorners(tagPose, tagSize);
		std::vector<cv::Point2d> projected;
		cv::projectPoints(fieldCorners, rvecWorldToCamera, tvecWorldToCamera, cameraMatrix, distCoeffs, projected);
		objectPoints.insert(objectPoints.end(), fieldCorners.begin(), fieldCorners.end());
		imagePoints.insert(imagePoints.end(), projected.begin(), projected.end());
	}

	nlohmann::json result = ApriltagDetector::SolveMultiTagPnP(objectPoints, imagePoints, cameraMatrix, distCoeffs, 2);

	REQUIRE_FALSE(result.is_null());
	REQUIRE(result["tagCount"].get<int>() == 2);
	// Noiseless data; 0.5px allows for solver differences across platforms.
	REQUIRE(result["reprojErrPixels"].get<double>() < 0.5);

	REQUIRE(std::abs(result["x"].get<double>() - cameraPositionInField[0]) < 1e-4);
	REQUIRE(std::abs(result["y"].get<double>() - cameraPositionInField[1]) < 1e-4);
	REQUIRE(std::abs(result["z"].get<double>() - cameraPositionInField[2]) < 1e-4);

	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 3; c++) {
			REQUIRE(std::abs(result["R"][r][c].get<double>() - rmatCameraInField.at<double>(r, c)) < 1e-4);
		}
	}
}

TEST_CASE("SolveMultiTagPnP returns null with fewer than two tags", "[multitag]") {
	std::vector<cv::Point3d> objectPoints = { {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0} };
	std::vector<cv::Point2d> imagePoints = { {100,100}, {200,100}, {200,200}, {100,200} };
	cv::Mat cameraMatrix = (cv::Mat_<double>(3, 3) << 900, 0, 640, 0, 900, 480, 0, 0, 1);
	cv::Mat distCoeffs = cv::Mat::zeros(4, 1, CV_64F);

	nlohmann::json result = ApriltagDetector::SolveMultiTagPnP(objectPoints, imagePoints, cameraMatrix, distCoeffs, 1);
	REQUIRE(result.is_null());
}
