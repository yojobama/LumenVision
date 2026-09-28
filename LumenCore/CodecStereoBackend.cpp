#ifdef LUMEN_WITH_CODEC_STEREO
#include "CodecStereoBackend.h"
#include <codec_stereo/cs.h>
#include <stdexcept>

namespace {
	// STEREO_BACKEND_CODEC_RKMPP is deliberately not a case here - see StereoDepthBackendKind.h.
	const char* BackendOverrideFor(StereoDepthBackendKind kind)
	{
		switch (kind) {
		case STEREO_BACKEND_CODEC_LAVC:        return "lavc_sw";
		case STEREO_BACKEND_CODEC_RKMPP_HWENC:  return "rkmpp_hwenc";
		default:                                return nullptr; // AUTO - cs_init()'s own probe order
		}
	}
}

CodecStereoBackend::CodecStereoBackend(const Config& cfg) : m_Cfg(cfg)
{
	cs_config csCfg{};
	csCfg.block_w = cfg.blockW;
	csCfg.block_h = cfg.blockH;
	csCfg.search_range_x = cfg.searchRangeX;
	csCfg.search_range_y = cfg.searchRangeY;
	csCfg.subpel = 0;
	csCfg.disparity_offset = cfg.disparityOffset;
	csCfg.backend_override = BackendOverrideFor(cfg.kind);
	csCfg.backend_params = nullptr;

	m_Ctx = cs_init(&csCfg);
	if (!m_Ctx) {
		throw std::runtime_error(
			std::string("CodecStereoBackend: cs_init failed for backend '") +
			(csCfg.backend_override ? csCfg.backend_override : "auto") +
			"' - not compiled in, or no usable device on this machine");
	}
}

CodecStereoBackend::~CodecStereoBackend()
{
	if (m_Ctx) cs_destroy(m_Ctx);
}

std::string CodecStereoBackend::Name() const
{
	return m_Ctx ? cs_get_backend_name(m_Ctx) : "none";
}

bool CodecStereoBackend::Compute(const cv::Mat& rectLeft, const cv::Mat& rectRight,
	std::vector<float>& disparityOut, int& cols, int& rows)
{
	if (!m_Ctx) return false;
	if (rectLeft.empty() || rectRight.empty() || rectLeft.size() != rectRight.size()) return false;
	if (rectLeft.type() != CV_8UC1 || rectRight.type() != CV_8UC1) return false;

	cs_frame left{}, right{};
	left.data[0] = rectLeft.data;
	left.data[1] = nullptr;
	left.stride[0] = (int)rectLeft.step;
	left.width = rectLeft.cols;
	left.height = rectLeft.rows;
	left.fmt = CS_PIX_FMT_GRAY8;

	right.data[0] = rectRight.data;
	right.data[1] = nullptr;
	right.stride[0] = (int)rectRight.step;
	right.width = rectRight.cols;
	right.height = rectRight.rows;
	right.fmt = CS_PIX_FMT_GRAY8;

	cs_mv_field mv{};
	if (cs_extract(m_Ctx, &left, &right, &mv) != 0) return false;

	cols = mv.cols;
	rows = mv.rows;
	disparityOut.assign((size_t)cols * rows, STEREO_DISPARITY_INVALID);

	cs_disparity_config dcfg{};
	dcfg.fx = 0.0f;              // unused here: only cs_disparity_to_depth needs fx/baseline,
	dcfg.baseline = 0.0f;        // and StereoDepthNode does the depth conversion
	dcfg.min_disparity = m_Cfg.minDisparity;
	dcfg.max_dy = m_Cfg.maxDy;
	dcfg.max_cost = m_Cfg.maxCost;

	cs_mv_field_to_disparity(&mv, &dcfg, disparityOut.data());

	if (m_Cfg.invertDisparitySign) {
		for (float& d : disparityOut) {
			if (d != STEREO_DISPARITY_INVALID) d = -d;
		}
	}

	return true;
}
#endif // LUMEN_WITH_CODEC_STEREO
