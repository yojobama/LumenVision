#pragma once
#include "IDetectionBackend.h"
#include <opencv2/opencv.hpp>
#include <vector>

// Backend-independent letterbox/decode/NMS math shared by every YOLOv8/v11 execution backend (RKNN, ONNX Runtime).
namespace YoloPostProcess {

	// scale/pad bookkeeping needed to map decoded boxes back to the original image
	struct LetterboxInfo {
		float scale;
		int padLeft;
		int padTop;
		int originalWidth;
		int originalHeight;
	};

	// resizes+pads bgrFrame to exactly targetWidth x targetHeight (YOLO convention: 114,114,114
	// fill), preserving aspect ratio, and returns the bookkeeping needed to undo it afterwards
	cv::Mat Letterbox(const cv::Mat& bgrFrame, int targetWidth, int targetHeight, LetterboxInfo& outInfo);

	// Decodes one anchor-free YOLOv8/11 head output [4 + numClasses, numAnchors] (batch stripped) into detections in the
	// original image's pixel space. `outputData` is channel-major (as ultralytics ONNX export produces); raw DFL distributions are not handled.
	std::vector<ObjectDetection> DecodeAndNms(
		const float* outputData,
		int numClasses,
		int numAnchors,
		const std::vector<std::string>& labels,
		const LetterboxInfo& letterbox,
		float confThreshold,
		float nmsThreshold);

	// The shared tail of every decoder: NMS over candidate boxes (in letterboxed-frame pixel space), then map survivors
	// back to the original frame's pixel coordinates.
	std::vector<ObjectDetection> NmsAndBuildDetections(
		const std::vector<cv::Rect>& boxesForNms,
		const std::vector<float>& scores,
		const std::vector<int>& classIds,
		const std::vector<std::string>& labels,
		const LetterboxInfo& letterbox,
		float confThreshold,
		float nmsThreshold);

	// One FPN scale's raw box/class tensors from an un-fused RKNN YOLOv8 export (rknn_model_zoo recipe), dequantised to float32
	// and NCHW with batch stripped: boxData is [4*regMax, gridH, gridW], clsData is [numClasses, gridH, gridW].
	struct DflScaleOutput {
		const float* boxData;
		const float* clsData;
		int gridH, gridW;
		int stride; // input pixels per grid cell at this scale (e.g. 8/16/32 for a 640 input)
	};

	// Decodes YOLOv8's DFL box regression (a per-side distribution over regMax bins, integrated to a distance) plus per-class
	// sigmoid scores across every FPN scale, then runs NMS. Differs from DecodeAndNms, whose boxes are regressed to cx/cy/w/h by the export graph.
	std::vector<ObjectDetection> DecodeDflMultiScaleAndNms(
		const std::vector<DflScaleOutput>& scales,
		int regMax,
		int numClasses,
		const std::vector<std::string>& labels,
		const LetterboxInfo& letterbox,
		float confThreshold,
		float nmsThreshold);

}
