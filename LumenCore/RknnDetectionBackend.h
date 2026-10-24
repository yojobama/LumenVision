#pragma once
#ifdef LUMEN_WITH_RKNN

#include "IDetectionBackend.h"
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
	void LastStageTimingsUs(std::vector<std::pair<std::string, double>>& out) const override { out = m_LastStageUs; }
	std::string Name() const override { return "RKNN (NPU)"; }

private:
	// one FPN scale of an un-fused multi-output DFL export (rknn_model_zoo recipe): outputs come in triples of
	// box[4*regMax,H,W], class[numClasses,H,W], scoreSum[1,H,W] (unused); see YoloPostProcess::DecodeDflMultiScaleAndNms
	struct ScaleMeta {
		uint32_t boxOutputIndex, clsOutputIndex;
		int gridH, gridW, stride, regMax;
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

	// the letterboxed picture and its RGB copy, kept between frames so nothing is allocated per frame
	cv::Mat m_Letterboxed, m_Rgb;

	// whether the class tensors hold probabilities or logits, decided from the first frame and kept (it is a property of the model)
	std::optional<bool> m_ClassScoresAreProbabilities;
};

#endif // LUMEN_WITH_RKNN
