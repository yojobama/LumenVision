#include <catch2/catch_test_macros.hpp>
#include "RoiSource.h"
#include <chrono>
#include <thread>

// Two RoiSources split a synthetic side-by-side frame (left black, right white) with a known
// captureTimeUs into left and right sources.

namespace {

class SyntheticSideBySideSource : public ISource {
public:
	SyntheticSideBySideSource(std::shared_ptr<Logger> logger, std::string id, int eyeWidth, int height)
		: ISource(logger, id), m_EyeWidth(eyeWidth), m_Height(height)
	{
	}

protected:
	void CaptureFrame() override {
		cv::Mat frame(m_Height, m_EyeWidth * 2, CV_8UC3);
		frame(cv::Rect(0, 0, m_EyeWidth, m_Height)).setTo(cv::Scalar(0, 0, 0));
		frame(cv::Rect(m_EyeWidth, 0, m_EyeWidth, m_Height)).setTo(cv::Scalar(255, 255, 255));
		// Known capture timestamp; RoiSource must propagate it unchanged.
		SetLatestResult(SourceResult(std::nullopt, frame, 123456789ULL));
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

private:
	int m_EyeWidth, m_Height;
};

}

TEST_CASE("RoiSource splits a side-by-side frame into two independent, zero-copy eyes", "[roi]") {
	auto logger = std::make_shared<Logger>("LumenCoreTests-roi.log");
	const int eyeWidth = 320, height = 240;

	auto upstream = std::make_shared<SyntheticSideBySideSource>(logger, "upstream", eyeWidth, height);
	RoiSource leftRoi(logger, "left-roi", cv::Rect(0, 0, eyeWidth, height));
	RoiSource rightRoi(logger, "right-roi", cv::Rect(eyeWidth, 0, eyeWidth, height));

	REQUIRE(leftRoi.BindSource(upstream));
	REQUIRE(rightRoi.BindSource(upstream));

	// RoiSource has no capture thread of its own, so toggle the ISink side (cast disambiguates).
	upstream->Toggle(true);
	static_cast<ISink&>(leftRoi).Toggle(true);
	static_cast<ISink&>(rightRoi).Toggle(true);

	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	upstream->Toggle(false);
	static_cast<ISink&>(leftRoi).Toggle(false);
	static_cast<ISink&>(rightRoi).Toggle(false);

	SourceResult leftResult = leftRoi.GetLatestResult();
	SourceResult rightResult = rightRoi.GetLatestResult();

	REQUIRE(leftResult.frame.has_value());
	REQUIRE(rightResult.frame.has_value());

	REQUIRE(leftResult.frame->size() == cv::Size(eyeWidth, height));
	REQUIRE(rightResult.frame->size() == cv::Size(eyeWidth, height));

	// pixel-correctness: the left crop is genuinely the black half, the right crop the white half
	REQUIRE(leftResult.frame->AsBgr().at<cv::Vec3b>(height / 2, eyeWidth / 2) == cv::Vec3b(0, 0, 0));
	REQUIRE(rightResult.frame->AsBgr().at<cv::Vec3b>(height / 2, eyeWidth / 2) == cv::Vec3b(255, 255, 255));

	// Both eyes must report the same upstream capture time (stereo pairing relies on it).
	REQUIRE(leftResult.captureTimeUs == 123456789ULL);
	REQUIRE(rightResult.captureTimeUs == 123456789ULL);
}

TEST_CASE("a crop can be moved while it runs, and follows the new rectangle on the next frame", "[roi]") {
	auto logger = std::make_shared<Logger>("LumenCoreTests-roi.log");
	const int eyeWidth = 320, height = 240;

	auto upstream = std::make_shared<SyntheticSideBySideSource>(logger, "upstream-moving", eyeWidth, height);
	RoiSource roi(logger, "moving-roi", cv::Rect(0, 0, eyeWidth, height));
	REQUIRE(roi.BindSource(upstream));
	upstream->Toggle(true);
	static_cast<ISink&>(roi).Toggle(true);
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	REQUIRE(roi.GetLatestResult().frame->size() == cv::Size(eyeWidth, height));

	// the camera "changed resolution": the crop is now the right-hand half of the same frame, half as tall
	roi.SetRoi(cv::Rect(eyeWidth, 0, eyeWidth, height / 2));
	REQUIRE(roi.GetRoi() == cv::Rect(eyeWidth, 0, eyeWidth, height / 2));
	std::this_thread::sleep_for(std::chrono::milliseconds(150));

	upstream->Toggle(false);
	static_cast<ISink&>(roi).Toggle(false);
	SourceResult moved = roi.GetLatestResult();
	REQUIRE(moved.frame->size() == cv::Size(eyeWidth, height / 2));
	REQUIRE(moved.frame->AsBgr().at<cv::Vec3b>(10, 10) == cv::Vec3b(255, 255, 255)); // now cut from the white half
}
