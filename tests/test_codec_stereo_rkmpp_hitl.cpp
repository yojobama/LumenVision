#include <catch2/catch_test_macros.hpp>
#include "CodecStereoBackend.h"
#include <opencv2/opencv.hpp>

// codec-stereo rkmpp_hwenc (hardware encoder motion vectors) disparity against a known shift.
// Self-skips if construction throws or Compute() cannot probe the VPU.
TEST_CASE("codec-stereo's rkmpp_hwenc backend recovers a known synthetic disparity from real VPU hardware", "[hitl][rkmpp]") {
	const int W = 640, H = 384; // multiple of 32x16 - rkmpp_hwenc forces that block size regardless of Config
	const int SHIFT = 16;       // known synthetic horizontal disparity, in pixels

	// Random noise: motion estimation needs texture to find correspondences.
	cv::Mat base(H, W + SHIFT, CV_8UC1);
	cv::randu(base, 0, 255);
	cv::Mat left = base(cv::Rect(SHIFT, 0, W, H)).clone();
	cv::Mat right = base(cv::Rect(0, 0, W, H)).clone();

	CodecStereoBackend::Config cfg;
	cfg.kind = STEREO_BACKEND_CODEC_RKMPP_HWENC;
	cfg.searchRangeX = 48;
	cfg.searchRangeY = 16;
	cfg.minDisparity = 0.0f;
	cfg.maxDy = 4;

	std::unique_ptr<CodecStereoBackend> backend;
	try {
		backend = std::make_unique<CodecStereoBackend>(cfg);
	} catch (const std::exception&) {
		SKIP("rkmpp_hwenc backend unavailable on this machine (no RK3588 VPU, or rockchip_mpp not installed - see install-deps.sh --with-mpp)");
	}

	REQUIRE(backend->Name() == "rkmpp_hwenc");

	std::vector<float> disparity;
	int cols = 0, rows = 0;
	if (!backend->Compute(left, right, disparity, cols, rows)) {
		SKIP("rkmpp_hwenc backend constructed but Compute() failed - no real RK3588 VPU/device-tree on this machine (see the mpp_platform/mpp_dma_heap errors above)");
	}

	int validCount = 0;
	double sum = 0.0;
	for (float d : disparity) {
		if (d != STEREO_DISPARITY_INVALID) { validCount++; sum += d; }
	}
	REQUIRE(validCount > static_cast<int>(disparity.size()) / 2); // most blocks should resolve on pure texture
	double meanDisparity = sum / validCount;
	// Within 2px of the known shift; motion search is block-quantised.
	REQUIRE(meanDisparity > SHIFT - 2);
	REQUIRE(meanDisparity < SHIFT + 2);
}
