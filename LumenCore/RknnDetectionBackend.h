#pragma once
#ifdef LUMEN_WITH_RKNN

#include "IDetectionBackend.h"
#include "RgaLetterbox.h"
#include <rknn_api.h>
#include <optional>
#include <string>
#include <vector>

// Runs a YOLOv8/v11 export on the RK3588's NPU via the RKNN runtime (librknnrt.so).
//
// Reuses YoloPostProcess::DecodeAndNms; rknn_output's want_float=1 does the int8/fp16 dequantisation.
class RknnDetectionBackend : public IDetectionBackend {
public:
	RknnDetectionBackend();
	~RknnDetectionBackend() override;

	bool Load(const DetectionBackendConfig& config) override;
	std::vector<ObjectDetection> Infer(const cv::Mat& bgrFrame) override;
	std::vector<ObjectDetection> Infer(const Frame& frame) override;
	void LastStageTimingsUs(std::vector<std::pair<std::string, double>>& out) const override { out = m_LastStageUs; }
	std::string Name() const override { return "RKNN (NPU)"; }

private:
	// one FPN scale of an un-fused multi-output DFL export (rknn_model_zoo recipe): outputs come in triples of
	// box[4*regMax,H,W], class[numClasses,H,W], scoreSum[1,H,W] (unused); see YoloPostProcess::DecodeDflMultiScaleAndNms
	struct ScaleMeta {
		uint32_t boxOutputIndex, clsOutputIndex;
		int gridH, gridW, stride, regMax;
		// the tensors' affine quantisation (real = (raw - zeroPoint) * scale), used when the outputs are decoded as int8
		float boxScale, clsScale;
		int boxZeroPoint, clsZeroPoint;
	};

	rknn_context m_Context = 0;
	bool Initialise(const DetectionBackendConfig& config);

	bool m_Loaded = false;
	std::vector<std::pair<std::string, double>> m_LastStageUs; // written by Infer(), read by the same thread right after
	bool m_IsMultiScaleDfl = false;

	DetectionBackendConfig m_Config;
	std::vector<std::string> m_Labels;

	rknn_tensor_attr m_InputAttr{};
	int m_InputWidth = 0, m_InputHeight = 0;
	int m_NumOutputs = 0;
	int m_NumClasses = 0;

	// n_output==1 case (a fused single-head export, as in ONNX): fallback for models not exported via the multi-output recipe
	rknn_tensor_attr m_FusedOutputAttr{};

	std::vector<ScaleMeta> m_Scales;

	// the model's RGB input (tightly packed, input width x height x 3) and the CPU path's letterboxed BGR picture, kept between frames so
	// nothing is allocated per frame
	cv::Mat m_Canvas, m_Letterboxed;
	// every output is int8 with usable quantisation parameters: they are fetched as they are and decoded directly, instead of the runtime
	// converting each value to float (LUMEN_RKNN_FLOAT_OUTPUT=1 forces the float path)
	bool m_QuantisedOutputs = false;
#ifdef LUMEN_WITH_RGA
	// scales and converts the frame into the canvas on the RGA; after a failure the CPU path takes over for good
	RgaLetterbox m_Rga;
	bool m_RgaFailed = false;
#endif

	// whether the class tensors hold probabilities or logits, decided from the first frame and kept (it is a property of the model)
	std::optional<bool> m_ClassScoresAreProbabilities;
};

#endif // LUMEN_WITH_RKNN
