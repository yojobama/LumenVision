#include <catch2/catch_test_macros.hpp>
#include "RknnDetectionBackend.h"
#include <filesystem>
#include <opencv2/imgcodecs.hpp>

// RKNN backend on bus.jpg: expects 1 bus and 3 people, correctly positioned. Built only with
// LUMEN_WITH_RKNN; self-skips if the model is absent.
TEST_CASE("RknnDetectionBackend recovers correct, well-positioned detections from a real photo", "[hitl][rknn]") {
	const std::string modelPath = "/opt/photonvision/photonvision_config/models/yolov8nCOCO.rknn";
	if (!std::filesystem::exists(modelPath)) {
		SKIP("PhotonVision's yolov8nCOCO.rknn is not present on this machine");
	}

	DetectionBackendConfig config;
	config.modelPath = modelPath;
	config.labelsPath = std::string(LUMEN_TEST_DATA_DIR) + "/coco_80_labels.txt";
	// int8 quantisation noise yields many low-confidence false positives at lower thresholds.
	config.confThreshold = 0.6f;
	config.nmsThreshold = 0.45f;

	RknnDetectionBackend backend;
	REQUIRE(backend.Load(config));

	cv::Mat frame = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
	REQUIRE_FALSE(frame.empty());

	std::vector<ObjectDetection> detections = backend.Infer(frame);

	int busCount = 0, personCount = 0;
	for (const ObjectDetection& d : detections) {
		if (d.GetClassName() == "bus") {
			busCount++;
	// The bus fills roughly the left two-thirds of the frame.
			cv::Rect2d box = d.GetBoundingBox();
			REQUIRE(box.width > frame.cols * 0.3);
			REQUIRE(box.x < frame.cols * 0.5);
		}
		if (d.GetClassName() == "person") personCount++;
	}

	REQUIRE(busCount == 1);
	REQUIRE(personCount >= 3);
}
