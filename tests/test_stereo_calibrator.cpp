#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "StereoCalibrator.h"
#include "calibration_synthetic.h"

using namespace synthetic;

namespace {

constexpr double kBaseline = 0.06;

struct StereoRig {
	std::shared_ptr<Logger> logger = std::make_shared<Logger>("LumenCoreTests-stereo-calibrator.log");
	std::atomic<uint64_t> clock{1000};
	std::shared_ptr<FrameSource> left = std::make_shared<FrameSource>(logger, "left", &clock);
	std::shared_ptr<FrameSource> right = std::make_shared<FrameSource>(logger, "right", &clock);
	std::shared_ptr<StereoCalibrator> calibrator = std::make_shared<StereoCalibrator>(logger, "stereo-cal");

	StereoRig() {
		REQUIRE(calibrator->BindSource(left));
		REQUIRE(calibrator->BindSource(right));
		calibrator->SetStereoRoles("left", "right");
		left->Toggle(true);
		right->Toggle(true);
		calibrator->ISink::Toggle(true);
	}
	~StereoRig() {
		calibrator->ISink::Toggle(false);
		left->Toggle(false);
		right->Toggle(false);
	}

	// The right camera sits kBaseline to the right of the left one, so the board is kBaseline further left in its frame.
	bool ShowPoseAndSave(int i) {
		cv::Vec3d rvec, t;
		Pose(i, rvec, t);
		clock += 40000; // a fresh shared capture time for this pose
		left->SetFrame(RenderBoard(rvec, t));
		right->SetFrame(RenderBoard(rvec, t - cv::Vec3d(kBaseline, 0, 0)));
		left->WaitForFrames(3);
		right->WaitForFrames(3);
		return calibrator->SaveStereoDetection();
	}
};

}

TEST_CASE("StereoCalibrator saves nothing until both eyes see the board", "[calibration][stereo]") {
	StereoRig rig;
	rig.left->SetFrame(cv::Mat(kHeight, kWidth, CV_8UC1, cv::Scalar(128)));
	rig.right->SetFrame(cv::Mat(kHeight, kWidth, CV_8UC1, cv::Scalar(128)));
	rig.left->WaitForFrames(3);
	rig.right->WaitForFrames(3);

	REQUIRE_FALSE(rig.calibrator->SaveStereoDetection());
	REQUIRE(rig.calibrator->GetPairCount() == 0);
}

TEST_CASE("StereoCalibrator rejects a ChArUco board", "[calibration][stereo]") {
	auto logger = std::make_shared<Logger>("LumenCoreTests-stereo-calibrator.log");
	StereoCalibrationBoardConfig config;
	config.type = BOARD_CHARUCO;
	REQUIRE_THROWS(StereoCalibrator(logger, "bad", config));
}

TEST_CASE("StereoCalibrator refuses to calibrate from fewer than eight pairs", "[calibration][stereo]") {
	StereoRig rig;
	for (int i = 0; i < 7; i++) REQUIRE(rig.ShowPoseAndSave(i));
	REQUIRE(rig.calibrator->GetPairCount() == 7);
	REQUIRE_THROWS(rig.calibrator->RunCalibration());
}

TEST_CASE("StereoCalibrator recovers the baseline of a synthetic stereo pair", "[calibration][stereo]") {
	StereoRig rig;
	for (int i = 0; i < 15; i++) REQUIRE(rig.ShowPoseAndSave(i));
	REQUIRE(rig.calibrator->GetPairCorners(0, "left").size() == static_cast<size_t>(kCols * kRows * 2));
	REQUIRE(rig.calibrator->GetPairCorners(0, "centre").empty());

	StereoCalibrationResult result = rig.calibrator->RunCalibration();

	REQUIRE_THAT(result.baselineMeters, Catch::Matchers::WithinRel(kBaseline, 0.05));
	REQUIRE_THAT(result.left.fx, Catch::Matchers::WithinRel(kFocal, 0.05));
	REQUIRE(result.epipolarRms < 0.5);
	REQUIRE(result.imageWidth == kWidth);
	REQUIRE(result.Q.size() == 16);
}
