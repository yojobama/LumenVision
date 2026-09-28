#include <catch2/catch_test_macros.hpp>
#include "SgbmStereoBackend.h"
#include <opencv2/opencv.hpp>
#include <algorithm>

// SgbmStereoBackend disparity against a synthetic pair with a known horizontal shift.
TEST_CASE("SgbmStereoBackend recovers a known synthetic disparity", "[stereo][sgbm]") {
	const int W = 640, H = 384;
	const int SHIFT = 32; // known synthetic horizontal disparity, in pixels

	// Blocky texture: per-pixel noise gives StereoSGBM nothing to lock onto.
	const int texelSize = 8; // each coarse cell becomes an 8x8 block of identical pixels
	cv::Mat coarse((H + texelSize - 1) / texelSize, (W + SHIFT + texelSize - 1) / texelSize, CV_8UC1);
	cv::randu(coarse, 0, 255);
	cv::Mat base;
	cv::resize(coarse, base, cv::Size(W + SHIFT, H), 0, 0, cv::INTER_NEAREST);
	cv::Mat left = base(cv::Rect(SHIFT, 0, W, H)).clone();
	cv::Mat right = base(cv::Rect(0, 0, W, H)).clone();

	// Search range tightly brackets the known shift.
	SgbmStereoBackend backend(/*blockW*/16, /*blockH*/16, /*minDisparity*/16, /*numDisparities*/32);
	REQUIRE(backend.Name() == "sgbm");

	std::vector<float> disparity;
	int cols = 0, rows = 0;
	REQUIRE(backend.Compute(left, right, disparity, cols, rows));
	REQUIRE(!disparity.empty());

	std::vector<float> validDisparities;
	validDisparities.reserve(disparity.size());
	for (float d : disparity) if (d != STEREO_DISPARITY_INVALID) validDisparities.push_back(d);
	REQUIRE(validDisparities.size() > disparity.size() / 2); // most blocks should resolve on pure texture

	// Median rather than mean, as outlier blocks skew the mean.
	std::nth_element(validDisparities.begin(), validDisparities.begin() + validDisparities.size() / 2, validDisparities.end());
	float medianDisparity = validDisparities[validDisparities.size() / 2];
	REQUIRE(medianDisparity > SHIFT - 5);
	REQUIRE(medianDisparity < SHIFT + 5);
}
