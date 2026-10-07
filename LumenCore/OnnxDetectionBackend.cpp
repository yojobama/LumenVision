#ifdef LUMEN_WITH_ONNX
#include "OnnxDetectionBackend.h"
#include "YoloPostProcess.h"
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <filesystem>
#endif

OnnxDetectionBackend::OnnxDetectionBackend()
	: m_Env(ORT_LOGGING_LEVEL_WARNING, "LumenVision")
	, m_MemoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
{
}

OnnxDetectionBackend::~OnnxDetectionBackend() = default;

bool OnnxDetectionBackend::Load(const DetectionBackendConfig& config)
{
	m_Config = config;
	m_ConfThreshold = config.confThreshold;
	m_NmsThreshold = config.nmsThreshold;

	Ort::SessionOptions options;
	options.SetIntraOpNumThreads(1);
	options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

	try {
		// ORTCHAR_T is wchar_t on Windows and char elsewhere; modelPath stays a UTF-8 std::string and is
		// widened only at this call.
#ifdef _WIN32
		std::wstring wideModelPath = std::filesystem::path(config.modelPath).wstring();
		m_Session = std::make_unique<Ort::Session>(m_Env, wideModelPath.c_str(), options);
#else
		m_Session = std::make_unique<Ort::Session>(m_Env, config.modelPath.c_str(), options);
#endif
	} catch (const Ort::Exception&) {
		return false;
	}

	if (m_Session->GetInputCount() != 1 || m_Session->GetOutputCount() != 1) {
		// a YOLOv8/v11 detection export has exactly one image input and one detection output
		return false;
	}

	Ort::AllocatorWithDefaultOptions allocator;
	m_InputName = m_Session->GetInputNameAllocated(0, allocator).get();
	m_OutputName = m_Session->GetOutputNameAllocated(0, allocator).get();

	m_Labels.clear();
	if (!config.labelsPath.empty()) {
		std::ifstream labelsFile(config.labelsPath);
		std::string line;
		while (std::getline(labelsFile, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (!line.empty()) m_Labels.push_back(line);
		}
	}

	return true;
}

std::vector<ObjectDetection> OnnxDetectionBackend::Infer(const cv::Mat& bgrFrame)
{
	if (!m_Session) return {};

	YoloPostProcess::LetterboxInfo letterboxInfo;
	cv::Mat letterboxed = YoloPostProcess::Letterbox(bgrFrame, m_Config.inputWidth, m_Config.inputHeight, letterboxInfo);

	// HWC BGR uint8 -> CHW RGB float32, normalised to [0,1] - the standard ultralytics export
	// preprocessing
	cv::Mat rgb;
	cv::cvtColor(letterboxed, rgb, cv::COLOR_BGR2RGB);
	rgb.convertTo(rgb, CV_32F, 1.0 / 255.0);

	std::vector<cv::Mat> channels(3);
	cv::split(rgb, channels);

	std::vector<float> inputTensorData;
	inputTensorData.reserve(static_cast<size_t>(3) * m_Config.inputWidth * m_Config.inputHeight);
	for (const cv::Mat& channel : channels) {
		inputTensorData.insert(inputTensorData.end(),
			reinterpret_cast<float*>(channel.data),
			reinterpret_cast<float*>(channel.data) + channel.total());
	}

	std::array<int64_t, 4> inputShape{ 1, 3, m_Config.inputHeight, m_Config.inputWidth };
	Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
		m_MemoryInfo, inputTensorData.data(), inputTensorData.size(), inputShape.data(), inputShape.size());

	const char* inputNames[] = { m_InputName.c_str() };
	const char* outputNames[] = { m_OutputName.c_str() };

	std::vector<Ort::Value> outputs = m_Session->Run(
		Ort::RunOptions{ nullptr }, inputNames, &inputTensor, 1, outputNames, 1);

	const Ort::Value& output = outputs[0];
	std::vector<int64_t> outputShape = output.GetTensorTypeAndShapeInfo().GetShape();
	if (outputShape.size() != 3 || outputShape[0] != 1) {
		return {}; // not the [1, 4+numClasses, numAnchors] shape this decoder expects
	}

	int channelCount = static_cast<int>(outputShape[1]);
	int numAnchors = static_cast<int>(outputShape[2]);
	int numClasses = channelCount - 4;
	if (numClasses <= 0) return {};

	const float* outputData = output.GetTensorData<float>();

	return YoloPostProcess::DecodeAndNms(
		outputData, numClasses, numAnchors, m_Labels, letterboxInfo,
		m_ConfThreshold.load(), m_NmsThreshold.load());
}

#endif // LUMEN_WITH_ONNX
