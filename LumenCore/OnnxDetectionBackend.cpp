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

	// the decoder reads float32; anything else (an fp16 export) would be misread, so refuse it up front
	Ort::TypeInfo inputTypeInfo = m_Session->GetInputTypeInfo(0);
	auto inputTensorInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
	if (inputTensorInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) return false;
	if (m_Session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) return false;

	// take the input size from the model, as the RKNN backend does: feeding a differently-sized tensor makes Run() throw.
	// A dynamic axis is reported as -1; the configured size is used for it.
	m_InputWidth = config.inputWidth;
	m_InputHeight = config.inputHeight;
	std::vector<int64_t> inputShape = inputTensorInfo.GetShape();
	if (inputShape.size() == 4) {
		if (inputShape[2] > 0) m_InputHeight = static_cast<int>(inputShape[2]);
		if (inputShape[3] > 0) m_InputWidth = static_cast<int>(inputShape[3]);
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
	cv::Mat letterboxed = YoloPostProcess::Letterbox(bgrFrame, m_InputWidth, m_InputHeight, letterboxInfo);

	// HWC BGR uint8 -> CHW RGB float32, normalised to [0,1] - the standard ultralytics export
	// preprocessing
	cv::Mat rgb;
	cv::cvtColor(letterboxed, rgb, cv::COLOR_BGR2RGB);
	rgb.convertTo(rgb, CV_32F, 1.0 / 255.0);

	// split straight into the CHW tensor: three planes that already have the destination shape are filled in place
	const size_t planeSize = static_cast<size_t>(m_InputWidth) * m_InputHeight;
	std::vector<float> inputTensorData(3 * planeSize);
	std::vector<cv::Mat> channels = {
		cv::Mat(m_InputHeight, m_InputWidth, CV_32F, inputTensorData.data()),
		cv::Mat(m_InputHeight, m_InputWidth, CV_32F, inputTensorData.data() + planeSize),
		cv::Mat(m_InputHeight, m_InputWidth, CV_32F, inputTensorData.data() + 2 * planeSize) };
	cv::split(rgb, channels);

	std::array<int64_t, 4> inputShape{ 1, 3, m_InputHeight, m_InputWidth };
	Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
		m_MemoryInfo, inputTensorData.data(), inputTensorData.size(), inputShape.data(), inputShape.size());

	const char* inputNames[] = { m_InputName.c_str() };
	const char* outputNames[] = { m_OutputName.c_str() };

	std::vector<Ort::Value> outputs;
	try {
		outputs = m_Session->Run(Ort::RunOptions{ nullptr }, inputNames, &inputTensor, 1, outputNames, 1);
	} catch (const Ort::Exception&) {
		return {}; // a failed frame must not take the sink's thread down
	}

	const Ort::Value& output = outputs[0];
	std::vector<int64_t> outputShape = output.GetTensorTypeAndShapeInfo().GetShape();
	if (outputShape.size() != 3 || outputShape[0] != 1) {
		return {}; // not the [1, 4+numClasses, numAnchors] shape this decoder expects
	}

	int channelCount = static_cast<int>(outputShape[1]);
	int numAnchors = static_cast<int>(outputShape[2]);
	const float* outputData = output.GetTensorData<float>();

	// Ultralytics exports [1, 4+numClasses, numAnchors]; some converters transpose it to [1, numAnchors, 4+numClasses].
	// Anchors always outnumber channels (8400 vs 84), which tells the two apart.
	std::vector<float> channelMajor;
	if (channelCount > numAnchors) {
		channelMajor = YoloPostProcess::ToChannelMajor(outputData, channelCount, numAnchors);
		outputData = channelMajor.data();
		std::swap(channelCount, numAnchors);
	}
	int numClasses = channelCount - 4;
	if (numClasses <= 0) return {};

	return YoloPostProcess::DecodeAndNms(
		outputData, numClasses, numAnchors, m_Labels, letterboxInfo,
		m_ConfThreshold.load(), m_NmsThreshold.load());
}

#endif // LUMEN_WITH_ONNX
