#if defined(LUMEN_WITH_WEBRTC) || defined(LUMEN_WITH_RECORD)
#include "Nv12Convert.h"

extern "C" {
#include <libavutil/opt.h>
}

Nv12ToAvFrame::~Nv12ToAvFrame()
{
	if (m_Sws) sws_freeContext(m_Sws);
}

bool Nv12ToAvFrame::Convert(const Frame& src, AVFrame* dst)
{
	if (!src.IsNv12() || !dst) return false;
	const cv::Mat& y = src.YPlane();
	const cv::Mat& uv = src.UvPlane();
	const auto dstFormat = static_cast<AVPixelFormat>(dst->format);
	if (dstFormat != AV_PIX_FMT_NV12 && dstFormat != AV_PIX_FMT_YUV420P) return false;

	// area averaging when shrinking (the previews are usually scaled down)
	const int flags = (dst->width < y.cols || dst->height < y.rows) ? SWS_AREA : SWS_BILINEAR;
	// The ranges must be set before the context is initialised: libswscale picks a plain plane copy for same-size NV12 to NV12, which ignores
	// them, and only the scaling path converts range. (sws_getCachedContext cannot take them, so the context is cached here.)
	const Key key{ y.cols, y.rows, dst->width, dst->height, dstFormat };
	if (!m_Sws || !(key == m_Key)) {
		if (m_Sws) sws_freeContext(m_Sws);
		m_Sws = sws_alloc_context();
		if (!m_Sws) return false;
		av_opt_set_int(m_Sws, "srcw", y.cols, 0);
		av_opt_set_int(m_Sws, "srch", y.rows, 0);
		av_opt_set_int(m_Sws, "src_format", AV_PIX_FMT_NV12, 0);
		av_opt_set_int(m_Sws, "dstw", dst->width, 0);
		av_opt_set_int(m_Sws, "dsth", dst->height, 0);
		av_opt_set_int(m_Sws, "dst_format", dstFormat, 0);
		av_opt_set_int(m_Sws, "sws_flags", flags, 0);
		// full-range JPEG YUV in, limited-range video out
		av_opt_set_int(m_Sws, "src_range", 1, 0);
		av_opt_set_int(m_Sws, "dst_range", 0, 0);
		if (sws_init_context(m_Sws, nullptr, nullptr) < 0) {
			sws_freeContext(m_Sws);
			m_Sws = nullptr;
			return false;
		}
		m_Key = key;
	}

	const uint8_t* srcData[4] = { y.data, uv.data, nullptr, nullptr };
	const int srcStride[4] = { static_cast<int>(y.step), static_cast<int>(uv.step), 0, 0 };
	return sws_scale(m_Sws, srcData, srcStride, 0, y.rows, dst->data, dst->linesize) == dst->height;
}

#endif
