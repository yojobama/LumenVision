#include <catch2/catch_test_macros.hpp>
#include "ApriltagDetector.h"
#include "CoordinateFrames.h"
#include <opencv2/calib3d.hpp>
#include <cmath>

// Builds a scene in WPILib terms (a camera pose in the field, a tag facing it), projects the tag corners through a pinhole
// camera, recovers the poses the way the detector does, and checks the converted results against the ground truth.

namespace {

using frames::Pose3;

cv::Matx33d ToMatx(const std::array<double, 9>& R) {
	return cv::Matx33d(R[0], R[1], R[2], R[3], R[4], R[5], R[6], R[7], R[8]);
}

std::array<double, 9> FromMatx(const cv::Matx33d& m) {
	return { m(0, 0), m(0, 1), m(0, 2), m(1, 0), m(1, 1), m(1, 2), m(2, 0), m(2, 1), m(2, 2) };
}

cv::Matx33d Rodrigues(double rx, double ry, double rz) {
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
