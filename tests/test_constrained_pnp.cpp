#include <catch2/catch_test_macros.hpp>
#include "ConstrainedPnp.h"
#include "frame_scene.h"
#include <cmath>

using namespace testscene;

namespace {

// a robot flat on the floor with a camera mounted 0.3 m forward, 0.2 m up and tilted 0.2 rad, and the scene that robot sees
struct RobotScene {
	Scene scene;
	double x = 3.0, y = 1.5, yaw = 0.3;
	cv::Matx33d mountRotation = Rodrigues(0.0, -0.2, 0.0);
	cv::Vec3d mountTranslation{ 0.3, 0.0, 0.2 };

	RobotScene() {
		const cv::Matx33d robotRotation = Rodrigues(0, 0, yaw);
		scene.cameraRotation = robotRotation * mountRotation;
		scene.cameraPosition = cv::Vec3d(x, y, 0.0) + robotRotation * mountTranslation;
	}

	ConstrainedPnpInput Input(size_t tagCount, double seedX, double seedY, double seedYaw) const {
		ConstrainedPnpInput input;
		for (size_t i = 0; i < tagCount; i++) {
			std::vector<cv::Point3d> field = scene.FieldCorners(scene.tags[i]);
			std::vector<cv::Point2d> pixels = scene.Project(field);
			input.fieldPoints.insert(input.fieldPoints.end(), field.begin(), field.end());
			input.imagePoints.insert(input.imagePoints.end(), pixels.begin(), pixels.end());
		}
		input.cameraMatrix = cv::Mat(scene.cameraMatrix);
		input.distCoeffs = cv::Mat::zeros(4, 1, CV_64F);
		input.robotToCamera.R = FromMatx(mountRotation);
		input.robotToCamera.t = { mountTranslation[0], mountTranslation[1], mountTranslation[2] };
		input.seedX = seedX;
		input.seedY = seedY;
		input.seedYaw = seedYaw;
		return input;
	}
};

}

TEST_CASE("the constrained solve recovers a flat robot pose from a poor seed", "[constrained]") {
	RobotScene s;
	ConstrainedPnpResult result = SolveConstrainedPnp(s.Input(2, s.x + 0.5, s.y - 0.4, s.yaw + 0.25));

	REQUIRE(result.ok);
	REQUIRE(std::abs(result.x - s.x) < 1e-5);
	REQUIRE(std::abs(result.y - s.y) < 1e-5);
	REQUIRE(std::abs(result.yaw - s.yaw) < 1e-5);
	REQUIRE(result.reprojErrPixels < 1e-3);
}

TEST_CASE("one tag is enough for the constrained solve", "[constrained]") {
	RobotScene s;
	ConstrainedPnpResult result = SolveConstrainedPnp(s.Input(1, s.x - 0.3, s.y + 0.3, s.yaw - 0.1));

	REQUIRE(result.ok);
	REQUIRE(std::abs(result.x - s.x) < 1e-3);
	REQUIRE(std::abs(result.y - s.y) < 1e-3);
	REQUIRE(std::abs(result.yaw - s.yaw) < 1e-3);
}

TEST_CASE("noisy corners move the constrained solution only slightly", "[constrained]") {
	RobotScene s;
	ConstrainedPnpInput input = s.Input(2, s.x + 0.2, s.y + 0.2, s.yaw + 0.1);
	// +-0.4 px of deterministic jitter on every corner
	for (int i = 0; i < static_cast<int>(input.imagePoints.size()); i++) {
		input.imagePoints[i].x += ((i * 7) % 5 - 2) * 0.2;
		input.imagePoints[i].y += ((i * 3) % 5 - 2) * 0.2;
	}
	ConstrainedPnpResult result = SolveConstrainedPnp(input);

	REQUIRE(result.ok);
	REQUIRE(std::abs(result.x - s.x) < 0.05);
	REQUIRE(std::abs(result.y - s.y) < 0.05);
	REQUIRE(std::abs(result.yaw - s.yaw) < 0.02);
	REQUIRE(result.reprojErrPixels < 1.0);
}

TEST_CASE("the constrained solve rejects degenerate input", "[constrained]") {
	RobotScene s;
	ConstrainedPnpInput empty = s.Input(0, 0, 0, 0);
	REQUIRE_FALSE(SolveConstrainedPnp(empty).ok);

	ConstrainedPnpInput mismatched = s.Input(1, s.x, s.y, s.yaw);
	mismatched.imagePoints.pop_back();
	REQUIRE_FALSE(SolveConstrainedPnp(mismatched).ok);

	// a seed facing away from the tags puts every point behind the camera
	REQUIRE_FALSE(SolveConstrainedPnp(s.Input(2, s.x, s.y, s.yaw + 3.0)).ok);
}
