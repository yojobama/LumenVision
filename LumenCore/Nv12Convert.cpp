#if defined(LUMEN_WITH_WEBRTC) || defined(LUMEN_WITH_RECORD)
#include "Nv12Convert.h"
#include "YuvRange.h"

bool Nv12ToAvFrame::Convert(const Frame& src, AVFrame* dst)
{
	if (!src.IsNv12() || !dst) return false;
	const auto format = static_cast<AVPixelFormat>(dst->format);
	if ((format != AV_PIX_FMT_NV12 && format != AV_PIX_FMT_YUV420P) || dst->width <= 0 || dst->height <= 0 || dst->width % 2 != 0 || dst->height % 2 != 0)
		return false;

	const cv::Mat& y = src.YPlane();
	const cv::Mat& uv = src.UvPlane();
	const bool scaled = dst->width != y.cols || dst->height != y.rows;
	// without the squeeze the planes are copied as they are (a full-range consumer such as a JPEG encoder)
	const uint8_t* lumaTable = m_SqueezeRange ? YuvRange::LumaTable() : nullptr;
	const uint8_t* chromaTable = m_SqueezeRange ? YuvRange::ChromaTable() : nullptr;

	// luma: shrink (area averaging) if the stream is smaller than the picture, then range-convert straight into the encoder's plane
	const cv::Mat* lumaSource = &y;
	if (scaled) {
		cv::resize(y, m_ScaledY, cv::Size(dst->width, dst->height), 0, 0, cv::INTER_AREA);
		lumaSource = &m_ScaledY;
	}
	YuvRange::ApplyTable(*lumaSource, dst->data[0], static_cast<size_t>(dst->linesize[0]), lumaTable);

	// chroma: the same, as interleaved UV pairs (one table serves both channels)
	const cv::Mat* chromaSource = &uv;
	if (scaled) {
		cv::resize(uv, m_ScaledUv, cv::Size(dst->width / 2, dst->height / 2), 0, 0, cv::INTER_AREA);
		chromaSource = &m_ScaledUv;
	}
	if (format == AV_PIX_FMT_NV12) {
		YuvRange::ApplyTable(*chromaSource, dst->data[1], static_cast<size_t>(dst->linesize[1]), chromaTable);
	} else {
		m_Chroma.create(chromaSource->rows, chromaSource->cols, CV_8UC2);
		YuvRange::ApplyTable(*chromaSource, m_Chroma.ptr<uint8_t>(), m_Chroma.step[0], chromaTable);
		cv::Mat dstU(dst->height / 2, dst->width / 2, CV_8UC1, dst->data[1], static_cast<size_t>(dst->linesize[1]));
		cv::Mat dstV(dst->height / 2, dst->width / 2, CV_8UC1, dst->data[2], static_cast<size_t>(dst->linesize[2]));
		cv::extractChannel(m_Chroma, dstU, 0);
		cv::extractChannel(m_Chroma, dstV, 1);
	}
	return true;
}

#endif
