#include <catch2/catch_test_macros.hpp>
#include "OnnxDetectionBackend.h"
#include <opencv2/imgcodecs.hpp>

// OnnxDetectionBackend on bus.jpg (YOLOv8n/COCO): expects 1 bus and at least 3 people, as in
// test_rknn_detection_hitl.cpp. Runs on the CPU execution provider.
TEST_CASE("OnnxDetectionBackend recovers correct, well-positioned detections from a real photo", "[onnx]") {
	DetectionBackendConfig config;
	config.modelPath = std::string(LUMEN_TEST_DATA_DIR) + "/yolov8n.onnx";
	config.labelsPath = std::string(LUMEN_TEST_DATA_DIR) + "/coco_80_labels.txt";
	config.variant = YOLOv8;
	config.confThreshold = 0.5f;
	config.nmsThreshold = 0.45f;
	config.inputWidth = 640;
	config.inputHeight = 640;

	OnnxDetectionBackend backend;
	REQUIRE(backend.Load(config));

	cv::Mat frame = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
	REQUIRE_FALSE(frame.empty());

	std::vector<ObjectDetection> detections = backend.Infer(frame);

	int busCount = 0, personCount = 0;
	for (const ObjectDetection& d : detections) {
		if (d.GetClassName() == "bus") {
			busCount++;
			// Positional sanity check on the box.
			cv::Rect2d box = d.GetBoundingBox();
			REQUIRE(box.width > frame.cols * 0.3);
			REQUIRE(box.x < frame.cols * 0.5);
		}
		if (d.GetClassName() == "person") personCount++;
	}

	REQUIRE(busCount == 1);
	REQUIRE(personCount >= 3);
}

TEST_CASE("the confidence cutoff of a loaded model can be changed between frames", "[onnx]") {
	DetectionBackendConfig config;
	config.modelPath = std::string(LUMEN_TEST_DATA_DIR) + "/yolov8n.onnx";
	config.labelsPath = std::string(LUMEN_TEST_DATA_DIR) + "/coco_80_labels.txt";
	config.variant = YOLOv8;
	config.confThreshold = 0.25f;
	config.nmsThreshold = 0.45f;

	OnnxDetectionBackend backend;
	REQUIRE(backend.Load(config));
	REQUIRE(backend.GetConfThreshold() == 0.25f);

	cv::Mat frame = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
	REQUIRE_FALSE(frame.empty());

	const size_t lenient = backend.Infer(frame).size();
	REQUIRE(lenient >= 4);

	backend.SetThresholds(0.9f, 0.45f);
	const std::vector<ObjectDetection> strict = backend.Infer(frame);
	REQUIRE(backend.GetConfThreshold() == 0.9f);
	REQUIRE(strict.size() < lenient);
	for (const ObjectDetection& d : strict) REQUIRE(d.GetConfidence() >= 0.9f);

	backend.SetThresholds(0.25f, 0.45f);
	REQUIRE(backend.Infer(frame).size() == lenient);
}
