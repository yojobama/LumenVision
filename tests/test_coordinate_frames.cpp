#include <catch2/catch_test_macros.hpp>
#include "ApriltagDetector.h"
#include "CoordinateFrames.h"
#include "frame_scene.h"
#include <opencv2/calib3d.hpp>
#include <cmath>

// Builds a scene in WPILib terms (a camera pose in the field, a tag facing it), projects the tag corners through a pinhole
// camera, recovers the poses the way the detector does, and checks the converted results against the ground truth.

using namespace testscene;

TEST_CASE("a tag's camera-to-tag pose converts to the WPILib frames", "[frames]") {
	Scene scene;
	const double h = scene.tagSize / 2.0;
	// libapriltag's own object points (its tag frame) in detection corner order
	const std::vector<cv::Point3d> aprilTagObjectPoints = { { -h, h, 0 }, { h, h, 0 }, { h, -h, 0 }, { -h, -h, 0 } };

	for (const auto& tag : scene.tags) {
		std::vector<cv::Point2d> pixels = scene.Project(scene.FieldCorners(tag));

		cv::Mat rvec, tvec;
		REQUIRE(cv::solvePnP(aprilTagObjectPoints, pixels, cv::Mat(scene.cameraMatrix), cv::Mat::zeros(4, 1, CV_64F), rvec, tvec,
			false, cv::SOLVEPNP_IPPE_SQUARE));
		cv::Mat rmat;
		cv::Rodrigues(rvec, rmat);

		Pose3 openCv;
		for (int r = 0; r < 3; r++) {
			for (int c = 0; c < 3; c++) openCv.R[r * 3 + c] = rmat.at<double>(r, c);
			openCv.t[r] = tvec.at<double>(r);
		}
		Pose3 wpilib = frames::AprilTagPoseToWpilib(openCv);

		// ground truth: the tag's pose in the WPILib camera frame
		cv::Matx33d cameraFromField = scene.cameraRotation.t();
		cv::Vec3d expectedT = cameraFromField * (tag.position - scene.cameraPosition);
		cv::Matx33d expectedR = cameraFromField * tag.rotation;

		for (int i = 0; i < 3; i++) REQUIRE(std::abs(wpilib.t[i] - expectedT[i]) < 1e-6);
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++) REQUIRE(std::abs(wpilib.R[r * 3 + c] - expectedR(r, c)) < 1e-6);
	}
}

TEST_CASE("a multi-tag solve converts to the camera's WPILib pose in the field", "[frames]") {
	Scene scene;
	std::vector<cv::Point3d> objectPoints;
	std::vector<cv::Point2d> imagePoints;
	for (const auto& tag : scene.tags) {
		std::vector<cv::Point3d> field = scene.FieldCorners(tag);
		std::vector<cv::Point2d> pixels = scene.Project(field);
		objectPoints.insert(objectPoints.end(), field.begin(), field.end());
		imagePoints.insert(imagePoints.end(), pixels.begin(), pixels.end());
	}

	nlohmann::json solved = ApriltagDetector::SolveMultiTagPnP(objectPoints, imagePoints, cv::Mat(scene.cameraMatrix),
		cv::Mat::zeros(4, 1, CV_64F), 2);
	REQUIRE_FALSE(solved.is_null());

	Pose3 openCv;
	openCv.t = { solved["x"], solved["y"], solved["z"] };
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++) openCv.R[r * 3 + c] = solved["R"][r][c];
	Pose3 wpilib = frames::OpenCvCameraInFieldToWpilib(openCv);

	for (int i = 0; i < 3; i++) REQUIRE(std::abs(wpilib.t[i] - scene.cameraPosition[i]) < 1e-4);
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++) REQUIRE(std::abs(wpilib.R[r * 3 + c] - scene.cameraRotation(r, c)) < 1e-4);
}

TEST_CASE("a tag facing the camera has a half-turn yaw and lies ahead on X", "[frames]") {
	// camera at the origin looking down +X, tag 3 m ahead facing back at it: OpenCV pose is identity rotation, z = 3
	Pose3 openCv;
	openCv.t = { 0, 0, 3.0 };
	Pose3 wpilib = frames::AprilTagPoseToWpilib(openCv);

	REQUIRE(std::abs(wpilib.t[0] - 3.0) < 1e-12);
	REQUIRE(std::abs(wpilib.t[1]) < 1e-12);
	REQUIRE(std::abs(wpilib.t[2]) < 1e-12);
	// a half turn about Z: X and Y flipped, Z unchanged
	REQUIRE(std::abs(wpilib.R[0] + 1.0) < 1e-12);
	REQUIRE(std::abs(wpilib.R[4] + 1.0) < 1e-12);
	REQUIRE(std::abs(wpilib.R[8] - 1.0) < 1e-12);

	// a tag to the camera's right (+X in OpenCV) is to the right of the robot, i.e. negative Y
	openCv.t = { 1.0, 0.0, 3.0 };
	REQUIRE(frames::AprilTagPoseToWpilib(openCv).t[1] < 0.0);
}

TEST_CASE("RotationToQuaternion returns unit quaternions for the principal rotations", "[frames]") {
	auto identity = frames::RotationToQuaternion({ 1, 0, 0, 0, 1, 0, 0, 0, 1 });
	REQUIRE(std::abs(identity[0] - 1.0) < 1e-12);

	// 90 degrees about Z: (w, x, y, z) = (cos 45, 0, 0, sin 45)
	auto quarterTurn = frames::RotationToQuaternion({ 0, -1, 0, 1, 0, 0, 0, 0, 1 });
	REQUIRE(std::abs(quarterTurn[0] - std::sqrt(0.5)) < 1e-12);
	REQUIRE(std::abs(quarterTurn[3] - std::sqrt(0.5)) < 1e-12);

	// 180 degrees about X exercises the largest-diagonal branch
	auto halfTurn = frames::RotationToQuaternion({ 1, 0, 0, 0, -1, 0, 0, 0, -1 });
	REQUIRE(std::abs(std::abs(halfTurn[1]) - 1.0) < 1e-12);
}
