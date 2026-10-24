#if defined(LUMEN_WITH_WEBRTC) || defined(LUMEN_WITH_RECORD)
#include "Nv12Convert.h"

namespace {

// full range (0-255 luma, 0-255 chroma centred on 128) to video range (16-235 luma, 16-240 chroma)
struct RangeTables {
	cv::Mat luma, chroma;
	RangeTables() : luma(1, 256, CV_8UC1), chroma(1, 256, CV_8UC1)
	{
		for (int v = 0; v < 256; v++) {
			luma.at<uchar>(v) = cv::saturate_cast<uchar>(16.0 + v * 219.0 / 255.0);
			chroma.at<uchar>(v) = cv::saturate_cast<uchar>(128.0 + (v - 128) * 224.0 / 255.0);
		}
	}
};

const RangeTables& Tables()
{
	static const RangeTables tables;
	return tables;
}

}

bool Nv12ToAvFrame::Convert(const Frame& src, AVFrame* dst)
{
	if (!src.IsNv12() || !dst) return false;
	const auto format = static_cast<AVPixelFormat>(dst->format);
	if ((format != AV_PIX_FMT_NV12 && format != AV_PIX_FMT_YUV420P) || dst->width <= 0 || dst->height <= 0 || dst->width % 2 != 0 || dst->height % 2 != 0)
		return false;

	const cv::Mat& y = src.YPlane();
	const cv::Mat& uv = src.UvPlane();
	const bool scaled = dst->width != y.cols || dst->height != y.rows;
	const RangeTables& tables = Tables();

	// luma: shrink (area averaging) if the stream is smaller than the picture, then range-convert straight into the encoder's plane
	const cv::Mat* lumaSource = &y;
	if (scaled) {
		cv::resize(y, m_ScaledY, cv::Size(dst->width, dst->height), 0, 0, cv::INTER_AREA);
		lumaSource = &m_ScaledY;
	}
	cv::Mat dstY(dst->height, dst->width, CV_8UC1, dst->data[0], static_cast<size_t>(dst->linesize[0]));
	cv::LUT(*lumaSource, tables.luma, dstY);

	// chroma: the same, as interleaved UV pairs (one table serves both channels)
	const cv::Mat* chromaSource = &uv;
	if (scaled) {
		cv::resize(uv, m_ScaledUv, cv::Size(dst->width / 2, dst->height / 2), 0, 0, cv::INTER_AREA);
		chromaSource = &m_ScaledUv;
	}
	if (format == AV_PIX_FMT_NV12) {
		cv::Mat dstUv(dst->height / 2, dst->width / 2, CV_8UC2, dst->data[1], static_cast<size_t>(dst->linesize[1]));
		cv::LUT(*chromaSource, tables.chroma, dstUv);
	} else {
		cv::LUT(*chromaSource, tables.chroma, m_Chroma);
		cv::Mat dstU(dst->height / 2, dst->width / 2, CV_8UC1, dst->data[1], static_cast<size_t>(dst->linesize[1]));
		cv::Mat dstV(dst->height / 2, dst->width / 2, CV_8UC1, dst->data[2], static_cast<size_t>(dst->linesize[2]));
		cv::extractChannel(m_Chroma, dstU, 0);
		cv::extractChannel(m_Chroma, dstV, 1);
	}
	return true;
}

#endif
