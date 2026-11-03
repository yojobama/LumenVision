#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "YoloPostProcess.h"
#ifdef LUMEN_WITH_ONNX
#include "OnnxDetectionBackend.h"
#include "onnx_synthetic.h"
#endif
#include <opencv2/opencv.hpp>
#include <cmath>

#ifdef LUMEN_WITH_ONNX
using namespace edge_onnx;

namespace {

// An "end-to-end" detection export: the network runs NMS itself and outputs [1, 300, 6] rows of (x1, y1, x2, y2, score, classId) in input
// pixels. That is what ultralytics writes for `export(format="onnx", nms=True)`, for YOLOv10 and for YOLO26 (end-to-end by default).
// The synthetic graph takes the first 1800 values of the image tensor (the red plane, normalised to [0,1]) as those rows and scales the
// four coordinates by 640, so the test decides the network's output by painting pixels.
std::string EndToEndModel()
{
	Graph g;
	g.inputs.push_back(ValueInfo("images", kFloat, { int64_t{1}, int64_t{3}, int64_t{640}, int64_t{640} }));
	g.outputs.push_back(ValueInfo("output0", kFloat, { int64_t{1}, int64_t{300}, int64_t{6} }));
	g.initializers.push_back(TensorInt64("flat_shape", { 1, -1 }));
	g.initializers.push_back(TensorInt64("starts", { 0 }));
	g.initializers.push_back(TensorInt64("ends", { 1800 }));
	g.initializers.push_back(TensorInt64("axes", { 1 }));
	g.initializers.push_back(TensorInt64("rows_shape", { 1, 300, 6 }));
	g.initializers.push_back(TensorFloat("scale", { 6 }, { 640, 640, 640, 640, 1, 1 }));
	g.nodes.push_back(Node("Reshape", { "images", "flat_shape" }, { "flat" }));
	g.nodes.push_back(Node("Slice", { "flat", "starts", "ends", "axes" }, { "first" }));
	g.nodes.push_back(Node("Reshape", { "first", "rows_shape" }, { "rows" }));
	g.nodes.push_back(Node("Mul", { "rows", "scale" }, { "output0" }));
	return WriteModel("lumen-edge-c-end2end.onnx", Model(g));
}

// a YOLO export with dynamic height and width (`export(format="onnx", dynamic=True)`); the graph itself is an identity
std::string DynamicInputModel()
{
	Graph g;
	g.inputs.push_back(ValueInfo("images", kFloat, { int64_t{1}, int64_t{3}, std::string("height"), std::string("width") }));
	g.outputs.push_back(ValueInfo("output0", kFloat, { int64_t{1}, int64_t{3}, std::string("height"), std::string("width") }));
	g.nodes.push_back(Node("Identity", { "images" }, { "output0" }));
	return WriteModel("lumen-edge-c-dynamic.onnx", Model(g));
}

// writes one network output value through the red channel of the pixel the synthetic graph reads it from
void PaintOutput(cv::Mat& bgr, int row, int column, float value, float scale)
{
	const int flat = row * 6 + column;
	const int y = flat / 640, x = flat % 640;
	bgr.at<cv::Vec3b>(y, x)[2] = cv::saturate_cast<uchar>(static_cast<int>(std::lround(value / scale * 255.0f)));
}

} // namespace

// OnnxDetectionBackend::Load only checks one input / one float output, and Infer accepts any [1, a, b] output: with a > b it assumes a
// transposed [1, anchors, 4+classes] head. An end-to-end export's [1, 300, 6] passes that test, so (x1, y1, x2, y2, score, class) is read
// as (cx, cy, w, h, class-0 score, class-1 score): every box is shifted up-left by half its size and clipped, and the class id is ignored.
// The detector runs and publishes plausible-looking but wrong boxes instead of refusing the model at upload.
TEST_CASE("C: an end-to-end (NMS-in-graph) ONNX export is refused or decoded as x1,y1,x2,y2,score,class", "[edge][detection]") {
	DetectionBackendConfig config;
	config.modelPath = EndToEndModel();
	config.confThreshold = 0.25f;
	OnnxDetectionBackend backend;
	if (!backend.Load(config)) {
		SUCCEED("the model was refused at load");
		return;
	}

	// one detection: class 0 at x1=100 y1=100 x2=300 y2=400, score 0.9; the other 299 rows are zero (no detection)
	cv::Mat frame(640, 640, CV_8UC3, cv::Scalar(0, 0, 0));
	PaintOutput(frame, 0, 0, 100, 640);
	PaintOutput(frame, 0, 1, 100, 640);
	PaintOutput(frame, 0, 2, 300, 640);
	PaintOutput(frame, 0, 3, 400, 640);
	PaintOutput(frame, 0, 4, 0.9f, 1);
	PaintOutput(frame, 0, 5, 0, 1);

	std::vector<ObjectDetection> detections = backend.Infer(frame);
	REQUIRE(detections.size() == 1);
	const cv::Rect2d box = detections[0].GetBoundingBox();
	INFO("box " << box.x << "," << box.y << " " << box.width << "x" << box.height << " class " << detections[0].GetClassId());
	CHECK(box.x == Catch::Approx(100).margin(4));
	CHECK(box.y == Catch::Approx(100).margin(4));
	CHECK(box.width == Catch::Approx(200).margin(4));
	CHECK(box.height == Catch::Approx(300).margin(4));
	CHECK(detections[0].GetClassId() == 0);
}

// The model's input size is taken from the network unless an axis is dynamic; then the upload's "inputSize" form field is used, which
// neither ModelController.Upload nor OnnxDetectionBackend::Load range-checks (the web UI's field accepts any typed integer but 0). With 0
// (or a negative size) Letterbox resizes to an empty size and cv::resize throws on every frame - outside Infer's try/catch, which only
// guards Run(), so on the detector's thread the service terminates (X1).
TEST_CASE("C: a dynamic-shape ONNX model with an input size of 0 is refused or survives a frame", "[edge][detection]") {
	DetectionBackendConfig config;
	config.modelPath = DynamicInputModel();
	config.inputWidth = 0;
	config.inputHeight = 0;
	OnnxDetectionBackend backend;
	if (!backend.Load(config)) {
		SUCCEED("refused at load");
		return;
	}
	cv::Mat frame(480, 640, CV_8UC3, cv::Scalar(90, 90, 90));
	REQUIRE_NOTHROW(backend.Infer(frame));
}

#endif // LUMEN_WITH_ONNX

// DecodeAndNms starts each anchor's best class at -1 with score 0 and keeps it whenever 0 >= confThreshold. A cutoff of 0 or below is
// rejected by SetObjectDetectionThresholds and UpdateModel, but not by objectDetectionSink/create's confThreshold nor model/upload's form
// field. An anchor whose class scores are all 0 (an int8/QDQ model's dequantised zero) then survives NMS (0 > negative cutoff) with
// classId -1, and NmsAndBuildDetections' bounds check `classId < labels.size()` passes for -1, so labels[-1] is read: a std::string
// copied from before the vector's heap block (ASan: heap-buffer-overflow; in Release, garbage or a crash). Hidden: it may crash.
TEST_CASE("C: a negative confidence cutoff never yields class id -1 or reads before the labels", "[edge][detection]") {
	const int numClasses = 1, numAnchors = 1;
	std::vector<float> out = { 320, 320, 64, 64, 0.0f }; // one box whose only class scores exactly 0
	YoloPostProcess::LetterboxInfo lb{ 1.0f, 0, 0, 640, 640 };
	const std::vector<std::string> labels = { "note" };

	std::vector<ObjectDetection> detections = YoloPostProcess::DecodeAndNms(out.data(), numClasses, numAnchors, labels, lb, -0.5f, 0.45f);
	for (const ObjectDetection& d : detections) {
		INFO("class " << d.GetClassId() << " name '" << d.GetClassName() << "'");
		CHECK(d.GetClassId() >= 0);
	}
}
