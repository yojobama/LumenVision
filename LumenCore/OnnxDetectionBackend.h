#pragma once
#ifdef LUMEN_WITH_ONNX

#include "IDetectionBackend.h"
#include <onnxruntime_cxx_api.h>
#include <memory>
#include <string>
#include <vector>

// Runs a YOLOv8/v11 ONNX export on stock ONNX Runtime (CPU execution provider only).
// Development/x86 path and portable fallback; RknnBackend is used on the Orange Pi.
class OnnxDetectionBackend : public IDetectionBackend {
public:
	OnnxDetectionBackend();
	~OnnxDetectionBackend() override;

	bool Load(const DetectionBackendConfig& config) override;
	std::vector<ObjectDetection> Infer(const cv::Mat& bgrFrame) override;
	std::string Name() const override { return "ONNX Runtime (CPU)"; }

private:
	Ort::Env m_Env;
	std::unique_ptr<Ort::Session> m_Session;
	Ort::MemoryInfo m_MemoryInfo;

	DetectionBackendConfig m_Config;
	std::vector<std::string> m_Labels;

	std::string m_InputName;
	std::string m_OutputName;
};

#endif // LUMEN_WITH_ONNX
