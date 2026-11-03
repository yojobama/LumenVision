#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "CameraCalibrator.h"
#include "StereoCalibrator.h"
#include "ObjectDetectionSink.h"
#include "YoloPostProcess.h"
#include "../../calibration_synthetic.h"
#include "edge_detection_util.h"
#include <atomic>
#include <cmath>
#include <future>
#include <thread>

using namespace edge_detection;

namespace {

std::shared_ptr<Logger> CalLogger()
{
	static auto logger = std::make_shared<Logger>("LumenCoreTests-edge-detection-cal.log");
	return logger;
}

cv::Mat BoardBgr(int pose, double scale = 1.0)
{
	cv::Vec3d rvec, t;
	synthetic::Pose(pose, rvec, t);
	cv::Mat gray = synthetic::RenderBoard(rvec, t);
	if (scale != 1.0) cv::resize(gray, gray, cv::Size(), scale, scale, cv::INTER_AREA);
	cv::Mat bgr;
	cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);
	return bgr;
}

// a backend that "finds" one fixed box in every frame
class OneBoxBackend : public IDetectionBackend {
public:
	bool Load(const DetectionBackendConfig&) override { return true; }
	std::vector<ObjectDetection> Infer(const cv::Mat& frame) override
	{
		ObjectDetection d;
		d.SetBoundingBox(cv::Rect2d(frame.cols / 4.0, frame.rows / 4.0, frame.cols / 2.0, frame.rows / 2.0));
		d.SetConfidence(0.9f);
		d.SetClassId(0);
		d.SetClassName("note");
		return { d };
	}
	std::string Name() const override { return "one-box"; }
};

} // namespace

// Neither Manager::CreateCameraCalibrator nor the Server (CalibrationController -> CalibrationSessionManager.StartCamera) checks the board size,
// and the web UI's Rows/Cols fields are bare number inputs (clearing one sends 0). cv::findChessboardCorners requires both counts > 2 and
// throws cv::Exception "Invalid pattern size" on every frame, on the calibrator's own thread: with X1 that terminates the service.
TEST_CASE("C: a camera calibrator for a 2-row checkerboard is refused or survives a frame", "[edge][detection]") {
	CalibrationBoardConfig config;
	config.type = BOARD_CHECKERBOARD;
	config.rows = 2;
	config.cols = 9;
	std::shared_ptr<CameraCalibrator> calibrator;
	try {
		calibrator = std::make_shared<CameraCalibrator>(CalLogger(), "edge-c-cal-2rows", config);
	} catch (const std::exception&) {
		SUCCEED("refused at construction");
		return;
	}
	SourceResult frame = FrameResult("cam", BoardBgr(0), SourceResult::NowUs());
	REQUIRE_NOTHROW(CallProcess(*calibrator, { frame }));
}

TEST_CASE("C: a camera calibrator for a 0x0 checkerboard (cleared UI fields) is refused or survives a frame", "[edge][detection]") {
	CalibrationBoardConfig config;
	config.type = BOARD_CHECKERBOARD;
	config.rows = 0;
	config.cols = 0;
	std::shared_ptr<CameraCalibrator> calibrator;
	try {
		calibrator = std::make_shared<CameraCalibrator>(CalLogger(), "edge-c-cal-0rows", config);
	} catch (const std::exception&) {
		SUCCEED("refused at construction");
		return;
	}
	SourceResult frame = FrameResult("cam", BoardBgr(0), SourceResult::NowUs());
	REQUIRE_NOTHROW(CallProcess(*calibrator, { frame }));
}

TEST_CASE("C: a stereo calibrator for a 2-row checkerboard is refused or survives a pair", "[edge][detection]") {
	StereoCalibrationBoardConfig config;
	config.type = BOARD_CHECKERBOARD;
	config.rows = 2;
	config.cols = 9;
	std::shared_ptr<StereoCalibrator> calibrator;
	try {
		calibrator = std::make_shared<StereoCalibrator>(CalLogger(), "edge-c-stereo-2rows", config);
	} catch (const std::exception&) {
		SUCCEED("refused at construction");
		return;
	}
	calibrator->SetStereoRoles("L", "R");
	const uint64_t t = SourceResult::NowUs();
	REQUIRE_NOTHROW(CallProcess(*calibrator, { FrameResult("L", BoardBgr(0), t), FrameResult("R", BoardBgr(1), t) }));
}

// StereoCalibrator::Process builds its preview with cv::hconcat(left, right), which throws unless both eyes have the same height. Two
// cameras at different modes (StartStereo takes any two camera sources and never compares their resolutions) therefore throw on the
// calibrator's thread on the first pair - with X1, the service terminates. Nothing else checks that the eyes match either:
// m_LastFrameSize is the left eye's alone.
TEST_CASE("C: a stereo calibrator fed eyes of different resolutions does not throw out of Process", "[edge][detection]") {
	auto calibrator = std::make_shared<StereoCalibrator>(CalLogger(), "edge-c-stereo-mismatch");
	calibrator->SetStereoRoles("L", "R");
	const uint64_t t = SourceResult::NowUs();
	cv::Mat left = BoardBgr(0);          // 1280x720
	cv::Mat right = BoardBgr(0, 0.5);    // 640x360
	REQUIRE_NOTHROW(CallProcess(*calibrator, { FrameResult("L", left, t), FrameResult("R", right, t) }));
	// and a pair whose eyes disagree on size must not become a saved calibration pair
	CHECK_FALSE(calibrator->SaveStereoDetection());
}

// The same missing check without the hconcat throw: eyes of equal height but different width (a 16:9 and a 4:3 mode, 1280x720 and
// 960x720) concatenate fine, so the pair is detected and saved, and RunCalibration later solves both eyes and stereoCalibrate at the left
// eye's size with right-eye corners measured in a different pixel grid.
TEST_CASE("C: a stereo calibrator refuses to save a pair whose eyes differ in width", "[edge][detection]") {
	auto calibrator = std::make_shared<StereoCalibrator>(CalLogger(), "edge-c-stereo-width");
	calibrator->SetStereoRoles("L", "R");
	const uint64_t t = SourceResult::NowUs();
	cv::Mat left = BoardBgr(0);                                       // 1280x720
	cv::Mat right = BoardBgr(1)(cv::Rect(160, 0, 960, 720)).clone(); // 960x720, the board still in view
	REQUIRE_NOTHROW(CallProcess(*calibrator, { FrameResult("L", left, t), FrameResult("R", right, t) }));
	// precondition: the board is seen by both eyes, so only the size mismatch can make the pair unsaveable
	SourceResult state = calibrator->GetLatestResult();
	REQUIRE(state.json.has_value());
	REQUIRE(state.json->value("foundLeft", false));
	REQUIRE(state.json->value("foundRight", false));
	CHECK_FALSE(calibrator->SaveStereoDetection());
}

// CameraCalibrator keeps every saved snapshot whatever the frame size and labels the result with the size of the LAST one saved. A camera
// whose mode changes during a session (the camera page can change it while calibrating) mixes two pixel scales into one cv::calibrateCamera
// and the result is stored for the newer resolution with intrinsics that fit neither.
TEST_CASE("C: camera calibration does not mix snapshots taken at two resolutions", "[edge][detection]") {
	auto calibrator = std::make_shared<CameraCalibrator>(CalLogger(), "edge-c-cal-mixed");
	int saved = 0;
	for (int i = 0; i < 6; i++) {
		CallProcess(*calibrator, { FrameResult("cam", BoardBgr(i), SourceResult::NowUs()) });
		if (calibrator->SaveBoardDetection()) saved++;
	}
	// the camera is switched to half resolution and the user carries on
	for (int i = 6; i < 12; i++) {
		CallProcess(*calibrator, { FrameResult("cam", BoardBgr(i, 0.5), SourceResult::NowUs()) });
		if (calibrator->SaveBoardDetection()) saved++;
	}
	REQUIRE(saved >= 10);

	CameraCalibrationResult result;
	try {
		result = calibrator->RunCalibration();
	} catch (const std::exception&) {
		SUCCEED("refused to calibrate across a resolution change");
		return;
	}
	// otherwise the result must be the half-resolution camera it is labelled as (fx 450, principal point 320,180)
	INFO("fx=" << result.fx << " cx=" << result.cx << " cy=" << result.cy << " rms=" << result.rms << " at " << result.imageWidth << "x" << result.imageHeight);
	REQUIRE(result.imageWidth == synthetic::kWidth / 2);
	CHECK(result.fx == Catch::Approx(synthetic::kFocal / 2).epsilon(0.05));
	CHECK(result.cx == Catch::Approx(synthetic::kWidth / 4.0).epsilon(0.05));
	CHECK(result.rms < 1.0);
}

// ObjectDetectionSink::SetCalibration accepts any non-empty distCoeffs; cv::undistortPoints only takes 4, 5, 8, 12 or 14 coefficients and
// throws for anything else - on the sink's thread, at the first detection (X1: the service terminates). A calibration reaches it from
// calibrations.json, which a settings-archive import replaces wholesale. ApriltagDetector takes the same vector unchecked.
TEST_CASE("C: an object detector given a calibration with 3 distortion coefficients survives a detection", "[edge][detection]") {
	auto sink = std::make_shared<ObjectDetectionSink>(CalLogger(), "edge-c-od-dist", std::make_shared<OneBoxBackend>());
	sink->SetCalibration(CameraCalibrationResult(600, 600, 320, 240, 0.3, { 0.1, -0.05, 0.001 }, 640, 480));
	cv::Mat frame(480, 640, CV_8UC3, cv::Scalar(40, 80, 120));
	REQUIRE_NOTHROW(CallProcess(*sink, { FrameResult("cam", frame, SourceResult::NowUs()) }));
}

// A box whose width overflows to +inf (an fp16 export overflowing, or a broken model) becomes x = -inf, width = inf; after unscaling,
// right = -inf + inf = NaN, std::clamp passes NaN through, and `origWidth <= 0` is false for NaN, so a detection with a NaN box is kept.
// The NaN box is published to the robot and to the web UI (where JSON serialises it as null), and DepthFusionNode casts NaN to int (UB).
TEST_CASE("C: a decoded box with an infinite size is dropped rather than reported with NaN coordinates", "[edge][detection]") {
	const int numClasses = 1, numAnchors = 2;
	// channel-major [4 + classes, anchors]
	std::vector<float> out(static_cast<size_t>(4 + numClasses) * numAnchors, 0.0f);
	auto at = [&](int channel, int anchor) -> float& { return out[static_cast<size_t>(channel) * numAnchors + anchor]; };
	at(0, 0) = 320; at(1, 0) = 320; at(2, 0) = std::numeric_limits<float>::infinity(); at(3, 0) = 50; at(4, 0) = 0.9f;
	at(0, 1) = 100; at(1, 1) = 100; at(2, 1) = 40; at(3, 1) = 40; at(4, 1) = 0.8f; // an ordinary box, kept
	YoloPostProcess::LetterboxInfo lb{ 1.0f, 0, 0, 640, 640 };

	std::vector<ObjectDetection> detections = YoloPostProcess::DecodeAndNms(out.data(), numClasses, numAnchors, { "note" }, lb, 0.25f, 0.45f);

	REQUIRE_FALSE(detections.empty());
	for (const ObjectDetection& d : detections) {
		const cv::Rect2d b = d.GetBoundingBox();
		INFO("box " << b.x << "," << b.y << " " << b.width << "x" << b.height);
		CHECK(std::isfinite(b.x));
		CHECK(std::isfinite(b.y));
		CHECK(std::isfinite(b.width));
		CHECK(std::isfinite(b.height));
	}
}

// RunCalibration reads m_ObjPoints / m_ImgPoints / frameSize and writes m_LastResult without m_DetectionMutex, while SaveBoardDetection,
// RemoveSnapshot and ClearSnapshots (each its own REST request: CalibrationSessionManager takes no lock around them) change the same
// vectors under it, and GetCalibrationResult copies m_LastResult unguarded. cv::calibrateCamera over a session's snapshots takes long
// enough for a "save" or "remove" click to land mid-solve: a data race (TSan), and a vector reallocated under the solver (ASan).
// Hidden: it may crash the process.
TEST_CASE("C: saving and removing snapshots while a calibration runs is safe", "[edge][detection]") {
	auto calibrator = std::make_shared<CameraCalibrator>(CalLogger(), "edge-c-cal-race");
	for (int i = 0; i < 8; i++) {
		CallProcess(*calibrator, { FrameResult("cam", BoardBgr(i), SourceResult::NowUs()) });
		REQUIRE(calibrator->SaveBoardDetection());
	}
	std::atomic<bool> stop{ false };
	std::thread editor([&] {
		int pose = 8;
		while (!stop) {
			CallProcess(*calibrator, { FrameResult("cam", BoardBgr(pose++ % 15), SourceResult::NowUs()) });
			calibrator->SaveBoardDetection();
			calibrator->RemoveSnapshot(calibrator->GetSnapshotCount() - 1);
		}
	});
	std::thread reader([&] {
		while (!stop) (void)calibrator->GetCalibrationResult().distCoeffs.size();
	});
	auto runs = std::async(std::launch::async, [&] {
		for (int i = 0; i < 6; i++) {
			try { calibrator->RunCalibration(); } catch (const std::exception&) {}
		}
	});
	const bool finished = runs.wait_for(std::chrono::seconds(120)) == std::future_status::ready;
	stop = true;
	editor.join();
	reader.join();
	REQUIRE(finished);
	SUCCEED("no crash; run under TSan/ASan for the race on CameraCalibrator's snapshot vectors and m_LastResult");
}
