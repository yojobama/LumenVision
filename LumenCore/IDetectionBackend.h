#pragma once
#include "ObjectDetection.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

// YOLOv8 and YOLOv11 (ultralytics ONNX export) share the same anchor-free head: [1, 4+numClasses, numAnchors], boxes decoded
// to (cx, cy, w, h) in model input pixels, no objectness channel. The enum is what the user selects and the manifest stores.
//
// Plain (unscoped) enum: SWIG wraps a scoped enum passed by value as an opaque handle, which fails at the P/Invoke
// boundary. ObjectDetectionProvider in Manager.h relies on the same behaviour.
enum YoloVariant {
	YOLOv8,
	YOLOv11
};

struct DetectionBackendConfig {
	std::string modelPath;
	std::string labelsPath; // one class name per line, in class-index order
	YoloVariant variant = YOLOv8;
	float confThreshold = 0.25f;
	float nmsThreshold = 0.45f;
	int inputWidth = 640;
	int inputHeight = 640;
};

class IDetectionBackend {
public:
	virtual ~IDetectionBackend() = default;
	virtual bool Load(const DetectionBackendConfig& config) = 0;
	virtual std::vector<ObjectDetection> Infer(const cv::Mat& bgrFrame) = 0;
	virtual std::string Name() const = 0;
};
