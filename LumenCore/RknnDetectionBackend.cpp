#ifdef LUMEN_WITH_RKNN
#include "RknnDetectionBackend.h"
#include "YoloPostProcess.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
	if (Initialise(config)) return true;
	// a half-built backend must not answer Infer() with a missing or wrongly-shaped model
	if (m_Loaded) rknn_destroy(m_Context);
	m_Loaded = false;
	return false;
}

bool RknnDetectionBackend::Initialise(const DetectionBackendConfig& config)
{
	if (m_Loaded) { rknn_destroy(m_Context); m_Loaded = false; }
	m_NumClasses = 0;
	m_ClassScoresAreProbabilities.reset();
	m_QuantisedOutputs = false;
	bool allQuantised = true;
	m_Config = config;
	m_ConfThreshold = config.confThreshold;
	m_NmsThreshold = config.nmsThreshold;

	std::ifstream modelFile(config.modelPath, std::ios::binary | std::ios::ate);
	if (!modelFile) return false;
	std::streamsize modelSize = modelFile.tellg();
	modelFile.seekg(0, std::ios::beg);
	std::vector<char> modelBuffer(static_cast<size_t>(modelSize));
	if (!modelFile.read(modelBuffer.data(), modelSize)) return false;

	int ret = rknn_init(&m_Context, modelBuffer.data(), static_cast<uint32_t>(modelSize), 0, nullptr);
	if (ret != RKNN_SUCC) return false;
	m_Loaded = true;

	// Each detector gets its own NPU core in turn (the default, AUTO, can queue two detectors behind one another); a failure here just
	// leaves the model on AUTO. The runtime and driver versions go to stderr (the journal) because a mismatch between them is the
	// first thing to rule out when a model misbehaves.
	static std::atomic<int> s_NextCore{ 0 };
	static const rknn_core_mask kCores[] = { RKNN_NPU_CORE_0, RKNN_NPU_CORE_1, RKNN_NPU_CORE_2 };
	const int coreIndex = s_NextCore++ % 3;
	const bool pinned = rknn_set_core_mask(m_Context, kCores[coreIndex]) == RKNN_SUCC;
	rknn_sdk_version version{};
	if (rknn_query(m_Context, RKNN_QUERY_SDK_VERSION, &version, sizeof(version)) == RKNN_SUCC) {
		std::fprintf(stderr, "RKNN runtime %s, driver %s, model %s on %s\n", version.api_version, version.drv_version,
			config.modelPath.c_str(), pinned ? ("NPU core " + std::to_string(coreIndex)).c_str() : "any NPU core");
	}

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
		// A fused head mixes box pixels and 0..1 scores in one tensor, so an int8 export's single scale zeroes every score: refuse it.
		// Export it as fp16 or as the un-fused multi-output layout.
		const bool quantised = m_FusedOutputAttr.type == RKNN_TENSOR_INT8 || m_FusedOutputAttr.type == RKNN_TENSOR_UINT8;
		if (quantised && m_FusedOutputAttr.scale > 0.05f) return false;
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

			m_Scales.push_back(ScaleMeta{ boxAttr.index, clsAttr.index, gridH, gridW, stride, regMax,
				boxAttr.scale, clsAttr.scale, boxAttr.zp, clsAttr.zp });
			const auto usable = [](const rknn_tensor_attr& attr) {
				return attr.type == RKNN_TENSOR_INT8 && attr.qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC && attr.scale > 0.0f;
			};
			if (!usable(boxAttr) || !usable(clsAttr)) allQuantised = false;
		}
	} else {
		return false; // neither the fused single-head layout nor a recognised multi-scale one
	}

	// int8 outputs are decoded as they are; the float path stays for fp16 exports and for LUMEN_RKNN_FLOAT_OUTPUT=1
	const char* forceFloat = std::getenv("LUMEN_RKNN_FLOAT_OUTPUT");
	m_QuantisedOutputs = m_IsMultiScaleDfl && allQuantised && !m_Scales.empty() && !(forceFloat && forceFloat[0] != '\0' && forceFloat[0] != '0');
	if (m_QuantisedOutputs) {
		m_ClassScoresAreProbabilities = YoloPostProcess::QuantisedClassScoresAreProbabilities(m_Scales[0].clsScale, m_Scales[0].clsZeroPoint);
	}
	m_Canvas = cv::Mat(m_InputHeight, m_InputWidth, CV_8UC3);
#ifdef LUMEN_WITH_RGA
	// LUMEN_RGA_DISABLE=1 keeps the input preparation on the CPU (for comparing the two paths)
	const char* noRga = std::getenv("LUMEN_RGA_DISABLE");
	m_RgaFailed = noRga && noRga[0] != '\0' && noRga[0] != '0';
#endif

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
	return Infer(Frame(bgrFrame));
}

std::vector<ObjectDetection> RknnDetectionBackend::Infer(const Frame& frame)
{
	if (!m_Loaded || frame.empty()) return {};

	m_LastStageUs.clear();
	auto lapStart = std::chrono::steady_clock::now();
	auto lap = [&](const char* stage) {
		auto now = std::chrono::steady_clock::now();
		m_LastStageUs.emplace_back(stage, std::chrono::duration<double, std::micro>(now - lapStart).count());
		lapStart = now;
	};

	// RGB uint8, NHWC, no normalisation: the quantised graph has mean/std baked in (the ONNX path normalises itself)
	YoloPostProcess::LetterboxInfo letterboxInfo;
	const cv::Rect picture = YoloPostProcess::LetterboxLayout(frame.size().width, frame.size().height, m_InputWidth, m_InputHeight, letterboxInfo);
	bool prepared = false;
#ifdef LUMEN_WITH_RGA
	// the RGA scales and converts the frame (NV12 or BGR) into its place in the canvas in one pass
	if (!m_RgaFailed) {
		prepared = m_Rga.Run(frame, m_Canvas.data, m_InputWidth, m_InputHeight, picture);
		if (!prepared) {
			m_RgaFailed = true;
			std::fprintf(stderr, "RKNN: the RGA could not prepare the detector input for this frame, preparing it on the CPU from now on\n");
		}
	}
	if (prepared) lap("rknn-rga");
#endif
	if (!prepared) {
		YoloPostProcess::LetterboxInto(frame.AsBgr(), m_InputWidth, m_InputHeight, letterboxInfo, m_Letterboxed);
		lap("rknn-letterbox");
		cv::cvtColor(m_Letterboxed, m_Canvas, cv::COLOR_BGR2RGB);
		lap("rknn-convert");
	}

	rknn_input input{};
	input.index = 0;
	input.buf = m_Canvas.data;
	input.size = static_cast<uint32_t>(m_Canvas.total() * m_Canvas.elemSize());
	input.pass_through = 0;
	input.type = RKNN_TENSOR_UINT8;
	input.fmt = RKNN_TENSOR_NHWC;

	if (rknn_inputs_set(m_Context, 1, &input) != RKNN_SUCC) return {};
	lap("rknn-set-input");
	if (rknn_run(m_Context, nullptr) != RKNN_SUCC) return {};
	lap("rknn-run");

	std::vector<rknn_output> outputs(static_cast<size_t>(m_NumOutputs));
	for (int i = 0; i < m_NumOutputs; i++) {
		outputs[i] = rknn_output{};
		outputs[i].want_float = m_QuantisedOutputs ? 0 : 1; // float: the runtime dequantises every value; int8: as the NPU wrote it
		outputs[i].index = static_cast<uint32_t>(i);
	}
	if (rknn_outputs_get(m_Context, static_cast<uint32_t>(m_NumOutputs), outputs.data(), nullptr) != RKNN_SUCC) return {};
	lap("rknn-get-outputs");

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
					m_Labels, letterboxInfo, m_ConfThreshold.load(), m_NmsThreshold.load());
			}
		}
	} else {
		if (m_QuantisedOutputs) {
			std::vector<YoloPostProcess::DflScaleOutputInt8> scaleOutputs;
			scaleOutputs.reserve(m_Scales.size());
			for (const ScaleMeta& scale : m_Scales) {
				scaleOutputs.push_back({ static_cast<const int8_t*>(outputs[scale.boxOutputIndex].buf), scale.boxScale, scale.boxZeroPoint,
					static_cast<const int8_t*>(outputs[scale.clsOutputIndex].buf), scale.clsScale, scale.clsZeroPoint,
					scale.gridH, scale.gridW, scale.stride });
			}
			detections = YoloPostProcess::DecodeDflMultiScaleInt8AndNms(scaleOutputs, m_Scales[0].regMax, m_NumClasses, m_Labels, letterboxInfo,
				m_ConfThreshold.load(), m_NmsThreshold.load(), m_ClassScoresAreProbabilities.value_or(true));
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
			if (!m_ClassScoresAreProbabilities.has_value())
				m_ClassScoresAreProbabilities = YoloPostProcess::ClassScoresAreProbabilities(scaleOutputs, m_NumClasses);
			detections = YoloPostProcess::DecodeDflMultiScaleAndNms(
				scaleOutputs, regMax, m_NumClasses, m_Labels, letterboxInfo,
				m_ConfThreshold.load(), m_NmsThreshold.load(), m_ClassScoresAreProbabilities);
		}
	}

	lap("rknn-decode+nms");
	rknn_outputs_release(m_Context, static_cast<uint32_t>(m_NumOutputs), outputs.data());
	return detections;
}

#endif // LUMEN_WITH_RKNN
