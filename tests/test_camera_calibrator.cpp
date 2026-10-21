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

namespace {

// The generated checkerboard PDF that prompted this test: 11 x 8 squares (its twelfth column is white and indistinguishable from the margin),
// so 10 x 7 interior corners, black where (row + col) is even.
cv::Mat PrintedBoardFrame()
{
	cv::Mat frame(720, 1280, CV_8UC1, cv::Scalar(255));
	const int square = 50, x0 = 340, y0 = 160;
	for (int r = 0; r < 8; r++)
		for (int c = 0; c < 11; c++)
			if ((r + c) % 2 == 0) cv::rectangle(frame, cv::Rect(x0 + c * square, y0 + r * square, square, square), cv::Scalar(0), cv::FILLED);
	cv::GaussianBlur(frame, frame, cv::Size(5, 5), 1.2);
	return frame;
}

// whether a calibrator configured with this pattern size accepts the frame as a board detection
bool DetectsPrintedBoard(int rows, int cols)
{
	auto logger = std::make_shared<Logger>("LumenCoreTests-camera-calibrator.log");
	std::atomic<uint64_t> clock{1000};
	auto source = std::make_shared<FrameSource>(logger, "cam", &clock);
	CalibrationBoardConfig config;
	config.rows = rows;
	config.cols = cols;
	auto calibrator = std::make_shared<CameraCalibrator>(logger, "cal", config);
	REQUIRE(calibrator->BindSource(source));
	source->Toggle(true);
	calibrator->ISink::Toggle(true);
	source->SetFrame(PrintedBoardFrame());
	source->WaitForFrames(3);
	bool found = calibrator->SaveBoardDetection();
	calibrator->ISink::Toggle(false);
	source->Toggle(false);
	return found;
}

}

TEST_CASE("an 11 x 8 square printed board is found only with its own interior corner counts", "[calibration][camera]") {
	CHECK(DetectsPrintedBoard(7, 10));      // 7 corners down, 10 across: what the printed board has
	CHECK(DetectsPrintedBoard(10, 7));      // the other orientation is accepted too
	CHECK_FALSE(DetectsPrintedBoard(11, 8)); // the square counts instead of the interior corner counts
	CHECK_FALSE(DetectsPrintedBoard(7, 11)); // one corner too many across
}

// Diagnostic, not part of the normal run: LUMEN_CALIB_IMAGE=<file> reports which pattern sizes and detector flags find a board in a real camera frame.
TEST_CASE("which chessboard detector settings find the board in a real frame", "[.][diagnostic][calibration]") {
	const char* path = std::getenv("LUMEN_CALIB_IMAGE");
	REQUIRE(path != nullptr);
	cv::Mat gray = cv::imread(path, cv::IMREAD_GRAYSCALE);
	REQUIRE_FALSE(gray.empty());
	const std::vector<std::pair<std::string, int>> flagSets = {
		{"adaptive+normalize+fast (what the calibrator uses)", cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_FAST_CHECK},
		{"adaptive+normalize", cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE},
		{"default", 0},
	};
	for (const auto& [name, flags] : flagSets) {
		for (cv::Size size : {cv::Size(11, 7), cv::Size(7, 11), cv::Size(11, 8), cv::Size(10, 7), cv::Size(12, 8)}) {
			std::vector<cv::Point2f> corners;
			bool found = cv::findChessboardCorners(gray, size, corners, flags);
			WARN(name << " " << size.width << "x" << size.height << ": " << (found ? "FOUND" : "no"));
		}
	}
	std::vector<cv::Point2f> corners;
	WARN("findChessboardCornersSB 11x7: " << cv::findChessboardCornersSB(gray, cv::Size(11, 7), corners));
}
