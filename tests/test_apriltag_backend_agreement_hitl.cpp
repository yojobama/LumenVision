#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "CpuApriltagBackend.h"
#include "VkApriltagBackend.h"
#include <opencv2/opencv.hpp>
#include <cmath>
#include <map>

// CpuApriltagBackend and VkApriltagBackend must agree on the reference image from the
// vkapriltag submodule (1280x800, tag36h11). Self-skips if no Vulkan compute device is found.
TEST_CASE("VkApriltagBackend::ResolveDecimation picks an integer the frame size divides by", "[apriltag]") {
	// Needs no GPU.
	REQUIRE(VkApriltagBackend::ResolveDecimation(0.0f, 1280, 800) == 2);  // default
	REQUIRE(VkApriltagBackend::ResolveDecimation(-1.0f, 1280, 800) == 2); // default
	REQUIRE(VkApriltagBackend::ResolveDecimation(1.0f, 1280, 800) == 1);
	REQUIRE(VkApriltagBackend::ResolveDecimation(4.0f, 1280, 800) == 4);
	REQUIRE(VkApriltagBackend::ResolveDecimation(1.5f, 1280, 800) == 2);  // rounds (no 1.5x path)
	REQUIRE(VkApriltagBackend::ResolveDecimation(0.4f, 1280, 800) == 1);  // never below 1
	REQUIRE(VkApriltagBackend::ResolveDecimation(3.0f, 1280, 800) == 2);  // 1280 % 3 != 0 -> steps down
	REQUIRE(VkApriltagBackend::ResolveDecimation(3.0f, 1920, 1080) == 3); // 1080p divides by 3
	REQUIRE(VkApriltagBackend::ResolveDecimation(4.0f, 1920, 1080) == 4); // 1080 % 4 == 0
	REQUIRE(VkApriltagBackend::ResolveDecimation(5.0f, 1920, 1080) == 5);
	REQUIRE(VkApriltagBackend::ResolveDecimation(7.0f, 1920, 1080) == 6); // 7 doesn't divide -> 6
}

TEST_CASE("CpuApriltagBackend and VkApriltagBackend agree on the same real image", "[hitl][vulkan][apriltag]") {
	const std::string pgmPath = std::string(LUMEN_VKAPRILTAG_SAMPLE_DIR) + "/grayimage.pgm";
	cv::Mat gray = cv::imread(pgmPath, cv::IMREAD_GRAYSCALE);
	REQUIRE_FALSE(gray.empty());

	// Both backends use the same decimation and refine setting; decimation does not affect
	// decoded tag IDs, so they must agree at every setting.
	const int decimation = GENERATE(1, 2, 4);
	const bool refine = GENERATE(false, true);
	INFO("decimation " << decimation << ", refineEdges " << refine);

	ApriltagTuning tuning;
	tuning.nthreads = 1;
	tuning.quadDecimate = static_cast<float>(decimation);
	tuning.refineEdges = refine;
	CpuApriltagBackend cpuBackend(tuning);

	std::unique_ptr<VkApriltagBackend> vkBackend;
	try {
		vkBackend = std::make_unique<VkApriltagBackend>(gray.cols, gray.rows, tuning);
	} catch (const std::exception& e) {
		SKIP("no usable Vulkan compute device on this machine (" << e.what() << ") - VkApriltagBackend "
			"falls back to CPU at the ApriltagDetector layer, but this test needs it to actually run standalone");
	}
	REQUIRE(vkBackend->GetQuadDecimate() == static_cast<float>(decimation));
	REQUIRE(vkBackend->GetRefineEdges() == refine);
	REQUIRE(cpuBackend.GetRefineEdges() == refine);

	zarray_t* cpuDetections = cpuBackend.Detect(gray);
	zarray_t* vkDetections = vkBackend->Detect(gray);

	auto extractById = [](zarray_t* detections) {
		std::map<int, apriltag_detection_t> byId;
		for (int i = 0; i < zarray_size(detections); i++) {
			apriltag_detection_t* det;
			zarray_get(detections, i, &det);
			byId[det->id] = *det;
		}
		return byId;
	};

	std::map<int, apriltag_detection_t> cpuById = extractById(cpuDetections);
	std::map<int, apriltag_detection_t> vkById = extractById(vkDetections);

	REQUIRE_FALSE(cpuById.empty()); // the reference image is known to contain a real tag
	REQUIRE(cpuById.size() == vkById.size());

	double sumSquaredCornerError = 0.0;
	int comparedCorners = 0;
	for (const auto& [id, cpuDet] : cpuById) {
		INFO("checking tag id " << id);
		REQUIRE(vkById.count(id) == 1); // same tag IDs decoded by both backends
		const apriltag_detection_t& vkDet = vkById.at(id);
		for (int corner = 0; corner < 4; corner++) {
			double dx = cpuDet.p[corner][0] - vkDet.p[corner][0];
			double dy = cpuDet.p[corner][1] - vkDet.p[corner][1];
			sumSquaredCornerError += dx * dx + dy * dy;
			comparedCorners++;
		}
	}

	cpuBackend.ReleaseResult(cpuDetections);
	vkBackend->ReleaseResult(vkDetections);

	REQUIRE(comparedCorners > 0);
	double cornerRmsPx = std::sqrt(sumSquaredCornerError / comparedCorners);
	// Tolerance is generous to allow for floating-point and driver drift.
	REQUIRE(cornerRmsPx < 2.0);
}
