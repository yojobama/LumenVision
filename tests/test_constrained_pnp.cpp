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

// ---- through the detector: a rendered tag, a field layout and a seed ----

#include "ApriltagDetector.h"
#include "ApriltagFamily.h"
#include <apriltag/common/image_u8.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {

class StillFrameSource : public ISource {
public:
	StillFrameSource(std::shared_ptr<Logger> logger, std::string id, cv::Mat bgr) : ISource(logger, id), m_Bgr(std::move(bgr)) {}

protected:
	void CaptureFrame() override {
		SetLatestResult(SourceResult(std::nullopt, m_Bgr));
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

private:
	cv::Mat m_Bgr;
};

// tag36h11 id `id` as the camera sees it: the tag picture warped onto the projected corners of the scene's tag, on a white wall
cv::Mat RenderScene(const RobotScene& robot, const Scene::Tag& tag, int id, cv::Size size)
{
	apriltag_family_t* family = CreateApriltagFamily(APRILTAG_FAMILY_36H11);
	image_u8_t* cells = apriltag_to_image(family, static_cast<uint32_t>(id));
	cv::Mat small(cells->height, cells->width, CV_8UC1, cells->buf, cells->stride);
	const int scale = 40, margin = 40;
	cv::Mat big, padded;
	cv::resize(small, big, cv::Size(), scale, scale, cv::INTER_NEAREST);
	cv::copyMakeBorder(big, padded, margin, margin, margin, margin, cv::BORDER_CONSTANT, cv::Scalar(255));
	const double w = big.cols, h = big.rows;
	image_u8_destroy(cells);
	DestroyApriltagFamily(APRILTAG_FAMILY_36H11, family);

	// WpilibTagCorners order: bottom-left, bottom-right, top-right, top-left as seen from the tag's front
	// apriltag_to_image draws a one-cell white frame around the black border, which is what the tag size measures
	const float lo = float(margin + scale), hiX = float(margin + w - scale), hiY = float(margin + h - scale);
	std::vector<cv::Point2f> from = { { lo, hiY }, { hiX, hiY }, { hiX, lo }, { lo, lo } };
	std::vector<cv::Point2f> to;
	for (const cv::Point2d& p : robot.scene.Project(robot.scene.FieldCorners(tag))) to.emplace_back(float(p.x), float(p.y));

	cv::Mat warped(size, CV_8UC1, cv::Scalar(255));
	cv::warpPerspective(padded, warped, cv::getPerspectiveTransform(from, to), size, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(255));
	cv::Mat bgr;
	cv::cvtColor(warped, bgr, cv::COLOR_GRAY2BGR);
	return bgr;
}

// a WPILib-format layout holding just this tag
std::string WriteLayout(const Scene::Tag& tag, int id)
{
	const auto q = frames::RotationToQuaternion(FromMatx(tag.rotation));
	const std::filesystem::path path = std::filesystem::temp_directory_path() / "lumencore-test-constrained-layout.json";
	std::ofstream(path) << "{\"tags\":[{\"ID\":" << id << ",\"pose\":{\"translation\":{\"x\":" << tag.position[0] << ",\"y\":" << tag.position[1]
		<< ",\"z\":" << tag.position[2] << "},\"rotation\":{\"quaternion\":{\"W\":" << q[0] << ",\"X\":" << q[1] << ",\"Y\":" << q[2]
		<< ",\"Z\":" << q[3] << "}}}}],\"field\":{\"length\":16.5,\"width\":8.2}}";
	return path.string();
}

nlohmann::json RunDetector(const cv::Mat& bgr, const std::string& layoutPath, bool seed, const RobotScene& robot)
{
	auto logger = std::make_shared<Logger>("LumenCoreTests-constrained-detector.log");
	auto source = std::make_shared<StillFrameSource>(logger, "constrained-source", bgr);
	CameraCalibrationResult calibration(1000, 1000, 640, 400, 0.1, {}, bgr.cols, bgr.rows);
	ApriltagDetector detector(logger, "constrained-detector", calibration, robot.scene.tagSize);

	REQUIRE(detector.LoadFieldLayout(layoutPath));
	if (seed) {
		frames::Pose3 mount;
		mount.R = FromMatx(robot.mountRotation);
		mount.t = { robot.mountTranslation[0], robot.mountTranslation[1], robot.mountTranslation[2] };
		// the seed is half a metre and a quarter radian off
		detector.SetConstrainedSeed(robot.x - 0.4, robot.y + 0.3, robot.yaw - 0.25, mount);
	}

	REQUIRE(detector.BindSource(source));
	source->Toggle(true);
	static_cast<ISink&>(detector).Toggle(true);
	std::this_thread::sleep_for(std::chrono::milliseconds(400));
	source->Toggle(false);
	static_cast<ISink&>(detector).Toggle(false);

	SourceResult result = detector.GetLatestResult();
	REQUIRE(result.json.has_value());
	return *result.json;
}

}

TEST_CASE("the detector solves the robot's floor pose from one visible tag and a seed", "[constrained][apriltag]") {
	RobotScene robot;
	const int id = 3;
	const cv::Mat image = RenderScene(robot, robot.scene.tags[0], id, cv::Size(1280, 800));
	const std::string layout = WriteLayout(robot.scene.tags[0], id);

	nlohmann::json withSeed = RunDetector(image, layout, true, robot);
	REQUIRE(withSeed["tags"].size() == 1);
	REQUIRE(withSeed["tags"][0]["id"] == id);
	REQUIRE_FALSE(withSeed["constrained"].is_null());
	REQUIRE(withSeed["constrained"]["tagCount"] == 1);
	// rendered pixels are quantised, so allow centimetres and a hundredth of a radian
	REQUIRE(std::abs(withSeed["constrained"]["x"].get<double>() - robot.x) < 0.05);
	REQUIRE(std::abs(withSeed["constrained"]["y"].get<double>() - robot.y) < 0.05);
	REQUIRE(std::abs(withSeed["constrained"]["yaw"].get<double>() - robot.yaw) < 0.02);
	REQUIRE(withSeed["constrained"]["reprojErrPixels"].get<double>() < 3.0);

	// no seed published yet: the solve stays off
	REQUIRE(RunDetector(image, layout, false, robot)["constrained"].is_null());
}
