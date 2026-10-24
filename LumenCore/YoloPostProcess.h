#pragma once
#include <optional>
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

	// The same picture as Letterbox, written into a canvas the caller keeps between frames: the frame is resized straight into its place in the
	// canvas and the grey border is only repainted when the layout changes, so a steady stream does no allocation and no extra copy.
	void LetterboxInto(const cv::Mat& bgrFrame, int targetWidth, int targetHeight, LetterboxInfo& outInfo, cv::Mat& canvas);

	// Rearranges a [numAnchors, channels] (anchor-major) head output, as some exporters produce, into the channel-major
	// [channels, numAnchors] layout DecodeAndNms reads.
	std::vector<float> ToChannelMajor(const float* anchorMajor, int numAnchors, int channels);

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

	// The shared tail of every decoder: NMS per class (as ultralytics does, so a person on a horse keeps both) over candidate boxes
	// (in letterboxed-frame pixel space), then map survivors back to the original frame's pixel coordinates, clipped to the frame.
	std::vector<ObjectDetection> NmsAndBuildDetections(
		const std::vector<cv::Rect2d>& boxesForNms,
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

	// Whether the class tensors hold probabilities (the rknn_model_zoo export applies the sigmoid in the graph) rather than raw logits.
	// Probabilities stay within [0,1] while the logits of a real model go well below 0, so one frame's range tells them apart; it is a
	// property of the model, so callers can ask once and hand the answer to DecodeDflMultiScaleAndNms.
	bool ClassScoresAreProbabilities(const std::vector<DflScaleOutput>& scales, int numClasses);

	// Decodes YOLOv8's DFL box regression (a per-side distribution over regMax bins, integrated to a distance) plus per-class
	// sigmoid scores across every FPN scale, then runs NMS. Differs from DecodeAndNms, whose boxes are regressed to cx/cy/w/h by the export graph.
	std::vector<ObjectDetection> DecodeDflMultiScaleAndNms(
		const std::vector<DflScaleOutput>& scales,
		int regMax,
		int numClasses,
		const std::vector<std::string>& labels,
		const LetterboxInfo& letterbox,
		float confThreshold,
		float nmsThreshold,
		std::optional<bool> classScoresAreProbabilities = std::nullopt); // nullopt = scan the class tensors for it on this call

}
