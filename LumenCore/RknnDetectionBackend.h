#pragma once
#ifdef LUMEN_WITH_RKNN

#include "IDetectionBackend.h"
#include <rknn_api.h>
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
	std::string Name() const override { return "RKNN (NPU)"; }

private:
	// one FPN scale of an un-fused multi-output DFL export (rknn_model_zoo recipe): outputs come in triples of
	// box[4*regMax,H,W], class[numClasses,H,W], scoreSum[1,H,W] (unused); see YoloPostProcess::DecodeDflMultiScaleAndNms
	struct ScaleMeta {
		uint32_t boxOutputIndex, clsOutputIndex;
		int gridH, gridW, stride, regMax;
	};

	rknn_context m_Context = 0;
	bool m_Loaded = false;
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
};

#endif // LUMEN_WITH_RKNN
