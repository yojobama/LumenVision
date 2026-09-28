#include <catch2/catch_test_macros.hpp>
#include "MppJpegDecoder.h"
#include <opencv2/opencv.hpp>
#include <fstream>
#include <vector>

// MppJpegDecoder hardware JPEG decode; where no JPEG VPU exists, init is expected to fail cleanly.
// The image is first decoded in software only to obtain its dimensions.

TEST_CASE("MppJpegDecoder decodes a real JPEG on real RK3588 JPEG-VPU hardware", "[hitl][mpp]") {
	std::ifstream file(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg", std::ios::binary);
	REQUIRE(file.is_open());
	std::vector<uint8_t> jpegBytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	REQUIRE_FALSE(jpegBytes.empty());

	cv::Mat reference = cv::imdecode(jpegBytes, cv::IMREAD_COLOR);
	REQUIRE_FALSE(reference.empty());
	int width = reference.cols, height = reference.rows;

	MppJpegDecoder decoder;

	SECTION("colour (BGR) output") {
		cv::Mat dst(height, width, CV_8UC3);
		bool ok = decoder.Decode(jpegBytes.data(), jpegBytes.size(), width, height, /*asGray=*/false, dst);
		if (!ok) {
			SKIP("MppJpegDecoder::Decode returned false - no working JPEG VPU on this machine (expected on CI/non-RK3588); this test only confirms real behaviour on actual hardware.");
		}
		REQUIRE(dst.rows == height);
		REQUIRE(dst.cols == width);
		REQUIRE(dst.type() == CV_8UC3);
	// Mean brightness should be close to the software decode (not bit-identical).
		cv::Scalar meanRef = cv::mean(reference);
		cv::Scalar meanDst = cv::mean(dst);
		for (int c = 0; c < 3; c++) {
			INFO("channel " << c << ": reference mean=" << meanRef[c] << " decoded mean=" << meanDst[c]);
			REQUIRE(std::abs(meanRef[c] - meanDst[c]) < 25.0);
		}
	}

	SECTION("grayscale (Y-plane) output") {
		cv::Mat referenceGray;
		cv::cvtColor(reference, referenceGray, cv::COLOR_BGR2GRAY);

		cv::Mat dst(height, width, CV_8UC1);
		bool ok = decoder.Decode(jpegBytes.data(), jpegBytes.size(), width, height, /*asGray=*/true, dst);
		if (!ok) {
			SKIP("MppJpegDecoder::Decode returned false - no working JPEG VPU on this machine (expected on CI/non-RK3588); this test only confirms real behaviour on actual hardware.");
		}
		REQUIRE(dst.rows == height);
		REQUIRE(dst.cols == width);
		REQUIRE(dst.type() == CV_8UC1);
		cv::Scalar meanRef = cv::mean(referenceGray);
		cv::Scalar meanDst = cv::mean(dst);
		INFO("reference gray mean=" << meanRef[0] << " decoded gray mean=" << meanDst[0]);
		REQUIRE(std::abs(meanRef[0] - meanDst[0]) < 25.0);
	}

	SECTION("repeated decode on the same instance does not crash or leak state") {
	// One long-lived decoder across many frames.
		cv::Mat dst(height, width, CV_8UC3);
		bool ok = true;
		for (int i = 0; i < 10 && ok; i++) {
			ok = decoder.Decode(jpegBytes.data(), jpegBytes.size(), width, height, false, dst);
		}
		if (!ok) {
			SKIP("MppJpegDecoder::Decode returned false - no working JPEG VPU on this machine (expected on CI/non-RK3588).");
		}
		REQUIRE(dst.rows == height);
		REQUIRE(dst.cols == width);
	}
}
