#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "CameraCalibrator.h"
#include "calibration_synthetic.h"

using namespace synthetic;

namespace {

struct CameraRig {
	std::shared_ptr<Logger> logger = std::make_shared<Logger>("LumenCoreTests-camera-calibrator.log");
	std::atomic<uint64_t> clock{1000};
	std::shared_ptr<FrameSource> source = std::make_shared<FrameSource>(logger, "cam", &clock);
	std::shared_ptr<CameraCalibrator> calibrator = std::make_shared<CameraCalibrator>(logger, "cal");

	CameraRig() {
		REQUIRE(calibrator->BindSource(source));
		source->Toggle(true);
		calibrator->ISink::Toggle(true);
	}
	~CameraRig() {
		calibrator->ISink::Toggle(false);
		source->Toggle(false);
	}

	bool ShowPoseAndSave(int i) {
		cv::Vec3d rvec, t;
		Pose(i, rvec, t);
		source->SetFrame(RenderBoard(rvec, t));
		source->WaitForFrames(3);
		return calibrator->SaveBoardDetection();
	}
};

}

TEST_CASE("CameraCalibrator saves nothing when no board is visible", "[calibration][camera]") {
	CameraRig rig;
	rig.source->SetFrame(cv::Mat(kHeight, kWidth, CV_8UC1, cv::Scalar(128)));
	rig.source->WaitForFrames(3);

	REQUIRE_FALSE(rig.calibrator->SaveBoardDetection());
	REQUIRE(rig.calibrator->GetSnapshotCount() == 0);
}

TEST_CASE("CameraCalibrator saves detections and manages snapshots", "[calibration][camera]") {
	CameraRig rig;
	REQUIRE(rig.ShowPoseAndSave(0));
	REQUIRE(rig.ShowPoseAndSave(1));
	REQUIRE(rig.calibrator->GetSnapshotCount() == 2);
	REQUIRE(rig.calibrator->GetFrameWidth() == kWidth);
	REQUIRE(rig.calibrator->GetFrameHeight() == kHeight);
	REQUIRE(rig.calibrator->GetSnapshotCorners(0).size() == static_cast<size_t>(kCols * kRows * 2));

	REQUIRE(rig.calibrator->RemoveSnapshot(0));
	REQUIRE_FALSE(rig.calibrator->RemoveSnapshot(5));
	REQUIRE(rig.calibrator->GetSnapshotCount() == 1);

	rig.calibrator->ClearSnapshots();
	REQUIRE(rig.calibrator->GetSnapshotCount() == 0);
}

TEST_CASE("CameraCalibrator refuses to calibrate from fewer than four snapshots", "[calibration][camera]") {
	CameraRig rig;
	for (int i = 0; i < 3; i++) REQUIRE(rig.ShowPoseAndSave(i));
	REQUIRE_THROWS(rig.calibrator->RunCalibration());
}

TEST_CASE("CameraCalibrator recovers the intrinsics of a synthetic camera", "[calibration][camera]") {
	CameraRig rig;
	for (int i = 0; i < 15; i++) REQUIRE(rig.ShowPoseAndSave(i));

	CameraCalibrationResult result = rig.calibrator->RunCalibration();

	REQUIRE(result.imageWidth == kWidth);
	REQUIRE(result.imageHeight == kHeight);
	REQUIRE(result.rms < 1.0);
	REQUIRE_THAT(result.fx, Catch::Matchers::WithinRel(kFocal, 0.05));
	REQUIRE_THAT(result.fy, Catch::Matchers::WithinRel(kFocal, 0.05));
	REQUIRE_THAT(result.cx, Catch::Matchers::WithinAbs(kWidth / 2.0, 20.0));
	REQUIRE_THAT(result.cy, Catch::Matchers::WithinAbs(kHeight / 2.0, 20.0));
	REQUIRE(rig.calibrator->GetCalibrationResult().fx == result.fx);
}
