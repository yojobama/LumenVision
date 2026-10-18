#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "RknnDetectionBackend.h"
#include <cstdlib>
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

// Models converted from ultralytics yolov8n/yolo11n (scripts: fused = one [1,84,8400] output, dfl = rknn_model_zoo's nine
// outputs). Point LUMEN_RKNN_MODEL_DIR at a directory holding <name>_<layout>_<precision>.rknn files; absent models are skipped.
TEST_CASE("RKNN YOLOv8 and YOLO11 exports, fused and multi-scale, each find the bus scene and no false positives", "[hitl][rknn]") {
	const char* dir = std::getenv("LUMEN_RKNN_MODEL_DIR");
	if (!dir) SKIP("LUMEN_RKNN_MODEL_DIR is not set");
	const std::string name = GENERATE(as<std::string>{},
		"yolov8n_fused_fp16", "yolov8n_dfl_fp16", "yolov8n_dfl_i8", "yolo11n_fused_fp16", "yolo11n_dfl_fp16", "yolo11n_dfl_i8");
	INFO(name);
	const std::string modelPath = std::string(dir) + "/" + name + ".rknn";
	if (!std::filesystem::exists(modelPath)) SKIP(modelPath << " is not present");

	DetectionBackendConfig config;
	config.modelPath = modelPath;
	config.labelsPath = std::string(LUMEN_TEST_DATA_DIR) + "/coco_80_labels.txt";
	config.confThreshold = 0.5f;
	config.nmsThreshold = 0.45f;
	RknnDetectionBackend backend;
	REQUIRE(backend.Load(config));

	cv::Mat frame = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
	REQUIRE_FALSE(frame.empty());
	std::vector<ObjectDetection> detections = backend.Infer(frame);

	// ultralytics finds exactly one bus and three people here
	int bus = 0, people = 0;
	for (const ObjectDetection& d : detections) {
		if (d.GetClassName() == "bus") bus++;
		if (d.GetClassName() == "person") people++;
	}
	REQUIRE(detections.size() == 4);
	REQUIRE(bus == 1);
	REQUIRE(people == 3);
}

TEST_CASE("an int8 fused-head export, whose single output scale cannot hold both boxes and scores, is refused at load", "[hitl][rknn]") {
	const char* dir = std::getenv("LUMEN_RKNN_MODEL_DIR");
	if (!dir) SKIP("LUMEN_RKNN_MODEL_DIR is not set");
	const std::string modelPath = std::string(dir) + "/yolov8n_fused_i8.rknn";
	if (!std::filesystem::exists(modelPath)) SKIP(modelPath << " is not present");
	DetectionBackendConfig config;
	config.modelPath = modelPath;
	RknnDetectionBackend backend;
	REQUIRE_FALSE(backend.Load(config));
}
