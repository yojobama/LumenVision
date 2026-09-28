#include <catch2/catch_test_macros.hpp>
#include "ApriltagDetector.h"
#include "CameraCalibrationResult.h"
#include <chrono>
#include <thread>

// Driver mode streams video but skips detection and NT4 publishing.

namespace {
class SyntheticFrameSource : public ISource {
public:
	SyntheticFrameSource(std::shared_ptr<Logger> logger, std::string id)
		: ISource(logger, id)
	{
	}

protected:
	void CaptureFrame() override {
		cv::Mat frame(240, 320, CV_8UC3, cv::Scalar(60, 90, 120));
		SetLatestResult(SourceResult(std::nullopt, frame));
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
};
}

TEST_CASE("ApriltagDetector's driver mode skips detection but still republishes frames", "[driver_mode]") {
	auto logger = std::make_shared<Logger>("LumenCoreTests-driver-mode.log");
	auto upstream = std::make_shared<SyntheticFrameSource>(logger, "driver-mode-upstream");

	// Default-constructed calibration (fx=fy=0) means no pose estimation.
	ApriltagDetector detector(logger, "driver-mode-detector", CameraCalibrationResult(), 0.1651);

	REQUIRE(detector.BindSource(upstream));
	detector.SetDriverMode(true);
	REQUIRE(detector.GetDriverMode());

	upstream->Toggle(true);
	static_cast<ISink&>(detector).Toggle(true);
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	upstream->Toggle(false);
	static_cast<ISink&>(detector).Toggle(false);

	SourceResult result = detector.GetLatestResult();
	REQUIRE(result.frame.has_value());
	REQUIRE_FALSE(result.frame->empty());
	REQUIRE(result.json.has_value());
	// Published envelope is {"tags": [...], "multiTag": ...}; both empty/null in driver mode.
	REQUIRE(result.json->is_object());
	REQUIRE(result.json->contains("tags"));
	REQUIRE((*result.json)["tags"].is_array());
	REQUIRE((*result.json)["tags"].empty()); // no detections published while driver mode is on
	REQUIRE((*result.json)["multiTag"].is_null());
}
