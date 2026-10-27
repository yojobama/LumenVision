#include <catch2/catch_test_macros.hpp>
#include "MjpegHwEncoder.h"
#include <opencv2/opencv.hpp>
#include <cmath>

// The RK3588's JPEG encoder (mjpeg_rkmpp) behind the MJPEG previews: a camera's NV12 frame and a BGR frame both come back as JPEGs that decode to the
// same picture, scaled when asked. Skips wherever there is no such encoder (x64, Windows, CI runners without the device).

namespace {

// a smooth picture (JPEG-friendly) with colour, as a full-range NV12 frame and as the BGR the same planes convert to
Frame GradientNv12(int width, int height)
{
	cv::Mat y(height, width, CV_8UC1), uv(height / 2, width / 2, CV_8UC2);
	for (int r = 0; r < height; r++)
		for (int c = 0; c < width; c++) y.at<uchar>(r, c) = static_cast<uchar>(16 + 200 * c / width);
	for (int r = 0; r < height / 2; r++)
		for (int c = 0; c < width / 2; c++) uv.at<cv::Vec2b>(r, c) = cv::Vec2b(static_cast<uchar>(96 + 64 * r / (height / 2)), static_cast<uchar>(160 - 64 * c / (width / 2)));
	return Frame::FromNv12(y, uv, nullptr);
}

double MeanAbsDiff(const cv::Mat& a, const cv::Mat& b)
{
	cv::Mat diff;
	cv::absdiff(a, b, diff);
	const cv::Scalar m = cv::mean(diff);
	return (m[0] + m[1] + m[2]) / 3.0;
}

}

TEST_CASE("the hardware JPEG encoder turns NV12 and BGR frames into JPEGs of the same picture", "[hitl][mjpeg][mpp]") {
	MjpegHwEncoder encoder;
	const Frame nv12 = GradientNv12(640, 480);
	std::vector<uint8_t> jpeg;
	if (!encoder.Encode(nv12, 90, 1, jpeg)) SKIP("no hardware JPEG encoder here: " + encoder.LastError());

	// NV12, full size: the decoded picture matches the planes' own BGR conversion (JPEG range kept, so no brightness shift)
	cv::Mat decoded = cv::imdecode(jpeg, cv::IMREAD_COLOR);
	REQUIRE(decoded.cols == 640);
	REQUIRE(decoded.rows == 480);
	CHECK(MeanAbsDiff(decoded, nv12.AsBgr()) < 4.0);

	// NV12, scaled by 2
	REQUIRE(encoder.Encode(nv12, 90, 2, jpeg));
	decoded = cv::imdecode(jpeg, cv::IMREAD_COLOR);
	REQUIRE(decoded.cols == 320);
	REQUIRE(decoded.rows == 240);

	// BGR (a detector's annotated frame): colour bars survive with their colours
	cv::Mat bgr(480, 640, CV_8UC3);
	const cv::Scalar bars[] = { { 255, 255, 255 }, { 0, 255, 255 }, { 255, 255, 0 }, { 0, 255, 0 }, { 255, 0, 255 }, { 0, 0, 255 }, { 255, 0, 0 }, { 0, 0, 0 } };
	for (int i = 0; i < 8; i++) bgr(cv::Rect(i * 80, 0, 80, 480)).setTo(bars[i]);
	REQUIRE(encoder.Encode(Frame(bgr), 90, 1, jpeg));
	decoded = cv::imdecode(jpeg, cv::IMREAD_COLOR);
	REQUIRE(decoded.size() == bgr.size());
	// away from the bar edges, where JPEG's chroma subsampling blurs
	for (int i = 0; i < 8; i++) {
		const cv::Rect inner(i * 80 + 16, 16, 48, 448);
		CHECK(MeanAbsDiff(decoded(inner), bgr(inner)) < 8.0);
	}

	// a lower quality makes a smaller file
	std::vector<uint8_t> high, low;
	REQUIRE(encoder.Encode(nv12, 95, 1, high));
	REQUIRE(encoder.Encode(nv12, 30, 1, low));
	CHECK(low.size() < high.size());
}
