#include <catch2/catch_test_macros.hpp>
#include "OnnxDetectionBackend.h"
#include <catch2/generators/catch_generators.hpp>
#include <opencv2/imgcodecs.hpp>
#include <cmath>

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

// Reference boxes are ultralytics' own predictions on bus.jpg (conf 0.5), as x1,y1,x2,y2.
TEST_CASE("both YOLO generations match ultralytics' reference detections on bus.jpg", "[onnx]") {
	struct Expected { const char* label; float x1, y1, x2, y2; };
	struct Case { const char* model; YoloVariant variant; std::vector<Expected> expected; };
	const Case testCase = GENERATE(
		Case{ "yolov8n.onnx", YOLOv8, { {"person", 211, 242, 283, 507}, {"person", 110, 236, 225, 536}, {"person", 477, 224, 560, 522}, {"bus", 99, 136, 551, 457} } },
		Case{ "yolo11n.onnx", YOLOv11, { {"bus", 90, 135, 552, 436}, {"person", 109, 236, 223, 536}, {"person", 212, 240, 285, 509}, {"person", 477, 231, 560, 521} } });
	INFO(testCase.model);

	DetectionBackendConfig config;
	config.modelPath = std::string(LUMEN_TEST_DATA_DIR) + "/" + testCase.model;
	config.labelsPath = std::string(LUMEN_TEST_DATA_DIR) + "/coco_80_labels.txt";
	config.variant = testCase.variant;
	config.confThreshold = 0.5f;
	config.nmsThreshold = 0.45f;

	OnnxDetectionBackend backend;
	REQUIRE(backend.Load(config));
	cv::Mat frame = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
	REQUIRE_FALSE(frame.empty());
	std::vector<ObjectDetection> detections = backend.Infer(frame);

	REQUIRE(detections.size() == testCase.expected.size());
	for (const Expected& want : testCase.expected) {
		bool found = false;
		for (const ObjectDetection& d : detections) {
			cv::Rect2d box = d.GetBoundingBox();
			if (d.GetClassName() == want.label && std::abs(box.x - want.x1) < 8 && std::abs(box.y - want.y1) < 8 &&
				std::abs(box.x + box.width - want.x2) < 8 && std::abs(box.y + box.height - want.y2) < 8) found = true;
		}
		INFO(want.label << " " << want.x1 << "," << want.y1 << "," << want.x2 << "," << want.y2);
		REQUIRE(found);
	}
}
