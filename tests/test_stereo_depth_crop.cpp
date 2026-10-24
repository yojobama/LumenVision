#include <catch2/catch_test_macros.hpp>
#include "Manager.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <chrono>
#include <filesystem>
#include <thread>

namespace {

std::vector<uint8_t> DecodeBase64(const std::string& text)
{
	static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::vector<uint8_t> out;
	uint32_t buffer = 0;
	int bits = 0;
	for (char c : text) {
		if (c == '=') break;
		buffer = (buffer << 6) | static_cast<uint32_t>(alphabet.find(c));
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
		}
	}
	return out;
}

// a calibration whose rectification is the identity (both eyes the same pinhole camera, no rotation), with the given valid regions
StereoCalibrationResult IdentityCalibration(int width, int height, cv::Rect roiLeft, cv::Rect roiRight)
{
	StereoCalibrationResult c;
	const double f = 600.0, cx = width / 2.0, cy = height / 2.0, baseline = 0.1;
	c.left = CameraCalibrationResult(f, f, cx, cy, 0.1, std::vector<double>(5, 0.0), width, height);
	c.right = c.left;
	c.R = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	c.T = { -baseline, 0, 0 };
	c.R1 = c.R;
	c.R2 = c.R;
	c.P1 = { f, 0, cx, 0, 0, f, cy, 0, 0, 0, 1, 0 };
	c.P2 = { f, 0, cx, -f * baseline, 0, f, cy, 0, 0, 0, 1, 0 };
	c.Q = { 1, 0, 0, -cx, 0, 1, 0, -cy, 0, 0, 0, f, 0, 0, 1 / baseline, 0 };
	c.baselineMeters = baseline;
	c.rectifiedFx = f;
	c.rectifiedCx = cx;
	c.rectifiedCy = cy;
	c.imageWidth = width;
	c.imageHeight = height;
	c.roiLeftX = roiLeft.x; c.roiLeftY = roiLeft.y; c.roiLeftW = roiLeft.width; c.roiLeftH = roiLeft.height;
	c.roiRightX = roiRight.x; c.roiRightY = roiRight.y; c.roiRightW = roiRight.width; c.roiRightH = roiRight.height;
	return c;
}

}

// The depth grid and output frame start at the top-left of the valid region, not at the rectified image's corner.
TEST_CASE("a StereoDepthNode crops at the valid region's origin", "[stereo]") {
	cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
	REQUIRE_FALSE(bus.empty());
	const std::filesystem::path clip = std::filesystem::temp_directory_path() / "lumencore-test-stereo-crop.avi";
	{
		cv::VideoWriter writer(clip.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 30.0, bus.size());
		REQUIRE(writer.isOpened());
		for (int i = 0; i < 300; i++) writer.write(bus);
	}

	Manager manager;
	const cv::Rect valid(48, 32, 528, 560); // not at the origin; 528 and 560 are multiples of the 16 px block
	StereoCalibrationResult calibration = IdentityCalibration(bus.cols, bus.rows, valid, valid);
	int leftId = manager.CreateVideoFileSource(clip.string(), 30);
	int rightId = manager.CreateVideoFileSource(clip.string(), 30);
	int nodeId = manager.CreateStereoDepthNode(STEREO_BACKEND_SGBM, calibration, 0.2, 20.0, 1000000, STEREO_FRAME_RECTIFIED_LEFT);
	REQUIRE(manager.BindStereoSources(nodeId, leftId, rightId));
	int previewId = manager.CreateMjpegSink(95);
	manager.BindSourceToSink(nodeId, previewId); // a bound sink that wants frames makes the node produce its output frame
	manager.StartSinkById(previewId);
	manager.StartSinkById(nodeId);
	manager.StartSourceById(leftId);
	manager.StartSourceById(rightId);

	cv::Mat output;
	for (int attempt = 0; attempt < 200 && output.empty(); attempt++) {
		std::string base64 = manager.GetMjpegFrameBase64(previewId);
		if (!base64.empty()) output = cv::imdecode(DecodeBase64(base64), cv::IMREAD_COLOR);
		if (output.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(25));
	}
	manager.StopSourceById(leftId);
	manager.StopSourceById(rightId);
	REQUIRE_FALSE(output.empty());
	REQUIRE(output.size() == cv::Size(valid.width, valid.height));

	// with an identity rectification the output is the source picture cut out at the valid region
	auto difference = [&](cv::Point origin) {
		cv::Mat expected = bus(cv::Rect(origin.x, origin.y, valid.width, valid.height));
		return cv::mean(cv::abs(output - expected))[0];
	};
	REQUIRE(difference({ valid.x, valid.y }) < 6.0);
	REQUIRE(difference({ 0, 0 }) > 20.0);

	manager.Shutdown();
	std::error_code ignored;
	std::filesystem::remove(clip, ignored);
}
