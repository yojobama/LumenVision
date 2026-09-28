#ifdef LUMEN_WITH_RKNN
#include "RknnDetectionBackend.h"
#include "YoloPostProcess.h"
#include <fstream>
#include <sstream>
#include <vector>

RknnDetectionBackend::RknnDetectionBackend() = default;

RknnDetectionBackend::~RknnDetectionBackend()
{
	if (m_Loaded) rknn_destroy(m_Context);
}

bool RknnDetectionBackend::Load(const DetectionBackendConfig& config)
{
	m_Config = config;

	std::ifstream modelFile(config.modelPath, std::ios::binary | std::ios::ate);
	if (!modelFile) return false;
	std::streamsize modelSize = modelFile.tellg();
	modelFile.seekg(0, std::ios::beg);
	std::vector<char> modelBuffer(static_cast<size_t>(modelSize));
	if (!modelFile.read(modelBuffer.data(), modelSize)) return false;

	int ret = rknn_init(&m_Context, modelBuffer.data(), static_cast<uint32_t>(modelSize), 0, nullptr);
	if (ret != RKNN_SUCC) return false;
	m_Loaded = true;

	rknn_input_output_num ioNum{};
	if (rknn_query(m_Context, RKNN_QUERY_IN_OUT_NUM, &ioNum, sizeof(ioNum)) != RKNN_SUCC) return false;
	if (ioNum.n_input != 1) return false; // a YOLOv8/v11 detection export has exactly one image input
	m_NumOutputs = static_cast<int>(ioNum.n_output);

	m_InputAttr = rknn_tensor_attr{};
	m_InputAttr.index = 0;
	if (rknn_query(m_Context, RKNN_QUERY_INPUT_ATTR, &m_InputAttr, sizeof(m_InputAttr)) != RKNN_SUCC) return false;

	// read the model's own input size (config.inputWidth/Height must match, else rknn_inputs_set feeds a wrongly-shaped
	// buffer); NHWC has H/W at dims[1]/dims[2], NCHW at dims[2]/dims[3]
	if (m_InputAttr.fmt == RKNN_TENSOR_NHWC) {
		m_InputHeight = static_cast<int>(m_InputAttr.dims[1]);
		m_InputWidth = static_cast<int>(m_InputAttr.dims[2]);
	} else {
		m_InputHeight = static_cast<int>(m_InputAttr.dims[2]);
		m_InputWidth = static_cast<int>(m_InputAttr.dims[3]);
	}

	if (m_NumOutputs == 1) {
		m_IsMultiScaleDfl = false;
		m_FusedOutputAttr = rknn_tensor_attr{};
		m_FusedOutputAttr.index = 0;
		if (rknn_query(m_Context, RKNN_QUERY_OUTPUT_ATTR, &m_FusedOutputAttr, sizeof(m_FusedOutputAttr)) != RKNN_SUCC) return false;
	} else if (m_NumOutputs % 3 == 0) {
		// airockchip/rknn_model_zoo yolov8 export: each FPN scale gives three outputs (box[4*regMax,H,W],
		// class[numClasses,H,W], scoreSum[1,H,W] - unused), ordered largest grid to smallest (stride 8, 16, 32 for a 640 input)
		m_IsMultiScaleDfl = true;
		m_Scales.clear();
		int numScales = m_NumOutputs / 3;
		for (int s = 0; s < numScales; s++) {
			rknn_tensor_attr boxAttr{};
			boxAttr.index = static_cast<uint32_t>(s * 3);
			if (rknn_query(m_Context, RKNN_QUERY_OUTPUT_ATTR, &boxAttr, sizeof(boxAttr)) != RKNN_SUCC) return false;

			rknn_tensor_attr clsAttr{};
			clsAttr.index = static_cast<uint32_t>(s * 3 + 1);
			if (rknn_query(m_Context, RKNN_QUERY_OUTPUT_ATTR, &clsAttr, sizeof(clsAttr)) != RKNN_SUCC) return false;

			// dims are [N, C, H, W] for these outputs regardless of the input tensor's NHWC layout
			int gridH = static_cast<int>(boxAttr.dims[2]);
			int gridW = static_cast<int>(boxAttr.dims[3]);
			int regMax = static_cast<int>(boxAttr.dims[1]) / 4;
			int numClasses = static_cast<int>(clsAttr.dims[1]);
			int stride = gridW > 0 ? m_InputWidth / gridW : 0;

			if (regMax <= 0 || numClasses <= 0 || stride <= 0) return false;
			if (m_NumClasses != 0 && m_NumClasses != numClasses) return false; // every scale must agree
			m_NumClasses = numClasses;

			m_Scales.push_back(ScaleMeta{ boxAttr.index, clsAttr.index, gridH, gridW, stride, regMax });
		}
	} else {
		return false; // neither the fused single-head layout nor a recognised multi-scale one
	}

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

std::vector<ObjectDetection> RknnDetectionBackend::Infer(const cv::Mat& bgrFrame)
{
	if (!m_Loaded) return {};

	YoloPostProcess::LetterboxInfo letterboxInfo;
	cv::Mat letterboxed = YoloPostProcess::Letterbox(bgrFrame, m_InputWidth, m_InputHeight, letterboxInfo);

	// RGB uint8, NHWC, no normalisation: the quantised graph has mean/std baked in (the ONNX path normalises itself)
	cv::Mat rgb;
	cv::cvtColor(letterboxed, rgb, cv::COLOR_BGR2RGB);

	rknn_input input{};
	input.index = 0;
	input.buf = rgb.data;
	input.size = static_cast<uint32_t>(rgb.total() * rgb.elemSize());
	input.pass_through = 0;
	input.type = RKNN_TENSOR_UINT8;
	input.fmt = RKNN_TENSOR_NHWC;

	if (rknn_inputs_set(m_Context, 1, &input) != RKNN_SUCC) return {};
	if (rknn_run(m_Context, nullptr) != RKNN_SUCC) return {};

	std::vector<rknn_output> outputs(static_cast<size_t>(m_NumOutputs));
	for (int i = 0; i < m_NumOutputs; i++) {
		outputs[i] = rknn_output{};
		outputs[i].want_float = 1; // dequantise int8/fp16 to float32
		outputs[i].index = static_cast<uint32_t>(i);
	}
	if (rknn_outputs_get(m_Context, static_cast<uint32_t>(m_NumOutputs), outputs.data(), nullptr) != RKNN_SUCC) return {};

	std::vector<ObjectDetection> detections;

	if (!m_IsMultiScaleDfl) {
		// the head shape shared with OnnxDetectionBackend: [1, 4+numClasses, numAnchors]
		if (m_FusedOutputAttr.n_dims == 3 || m_FusedOutputAttr.n_dims == 2) {
			// n_dims==3 -> [1, C, N]; n_dims==2 -> [C, N] (some conversions drop the batch dim)
			uint32_t channelCount = m_FusedOutputAttr.n_dims == 3 ? m_FusedOutputAttr.dims[1] : m_FusedOutputAttr.dims[0];
			uint32_t numAnchors = m_FusedOutputAttr.n_dims == 3 ? m_FusedOutputAttr.dims[2] : m_FusedOutputAttr.dims[1];
			int numClasses = static_cast<int>(channelCount) - 4;
			if (numClasses > 0) {
				detections = YoloPostProcess::DecodeAndNms(
					static_cast<const float*>(outputs[0].buf), numClasses, static_cast<int>(numAnchors),
					m_Labels, letterboxInfo, m_Config.confThreshold, m_Config.nmsThreshold);
			}
		}
	} else {
		std::vector<YoloPostProcess::DflScaleOutput> scaleOutputs;
		scaleOutputs.reserve(m_Scales.size());
		for (const ScaleMeta& scale : m_Scales) {
			YoloPostProcess::DflScaleOutput s;
			s.boxData = static_cast<const float*>(outputs[scale.boxOutputIndex].buf);
			s.clsData = static_cast<const float*>(outputs[scale.clsOutputIndex].buf);
			s.gridH = scale.gridH;
			s.gridW = scale.gridW;
			s.stride = scale.stride;
			scaleOutputs.push_back(s);
		}
		int regMax = m_Scales.empty() ? 0 : m_Scales[0].regMax;
		detections = YoloPostProcess::DecodeDflMultiScaleAndNms(
			scaleOutputs, regMax, m_NumClasses, m_Labels, letterboxInfo,
			m_Config.confThreshold, m_Config.nmsThreshold);
	}

	rknn_outputs_release(m_Context, static_cast<uint32_t>(m_NumOutputs), outputs.data());
	return detections;
}

#endif // LUMEN_WITH_RKNN
