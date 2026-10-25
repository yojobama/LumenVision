#if defined(LUMEN_WITH_WEBRTC) || defined(LUMEN_WITH_RECORD)
#include "Nv12Convert.h"
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

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

// Applies a 256-entry table to every byte of `src` (a continuous row of `n` bytes) into `dst`. On ARM64 this is four chained 64-byte NEON table
// lookups per 16 bytes (vqtbl4q gives 0 for an index out of its range, so the four partial results OR together): the same bytes as the scalar loop,
// 2.7 times faster on an A76 core and 3.4 times on an A55 (measured on an Orange Pi 5 Plus). cv::LUT, which this replaced, was one scalar load per byte
// and 8% of the service's CPU with two recordings running.
void ApplyTable(const uint8_t* src, uint8_t* dst, size_t n, const uint8_t* table)
{
	size_t i = 0;
#if defined(__aarch64__) && defined(__ARM_NEON)
	const uint8x16x4_t t0 = vld1q_u8_x4(table), t1 = vld1q_u8_x4(table + 64), t2 = vld1q_u8_x4(table + 128), t3 = vld1q_u8_x4(table + 192);
	const uint8x16_t k64 = vdupq_n_u8(64), k128 = vdupq_n_u8(128), k192 = vdupq_n_u8(192);
	for (; i + 16 <= n; i += 16) {
		const uint8x16_t v = vld1q_u8(src + i);
		uint8x16_t r = vqtbl4q_u8(t0, v);
		r = vorrq_u8(r, vqtbl4q_u8(t1, vsubq_u8(v, k64)));
		r = vorrq_u8(r, vqtbl4q_u8(t2, vsubq_u8(v, k128)));
		r = vorrq_u8(r, vqtbl4q_u8(t3, vsubq_u8(v, k192)));
		vst1q_u8(dst + i, r);
	}
#endif
	for (; i < n; i++) dst[i] = table[src[i]];
}

// the same over a whole plane, row by row (rows are the plane's own width, so padding between rows is never touched)
void ApplyTable(const cv::Mat& src, uint8_t* dst, size_t dstStep, const uint8_t* table)
{
	const size_t rowBytes = static_cast<size_t>(src.cols) * src.elemSize();
	for (int row = 0; row < src.rows; row++) ApplyTable(src.ptr<uint8_t>(row), dst + static_cast<size_t>(row) * dstStep, rowBytes, table);
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
	ApplyTable(*lumaSource, dst->data[0], static_cast<size_t>(dst->linesize[0]), tables.luma.ptr<uint8_t>());

	// chroma: the same, as interleaved UV pairs (one table serves both channels)
	const cv::Mat* chromaSource = &uv;
	if (scaled) {
		cv::resize(uv, m_ScaledUv, cv::Size(dst->width / 2, dst->height / 2), 0, 0, cv::INTER_AREA);
		chromaSource = &m_ScaledUv;
	}
	if (format == AV_PIX_FMT_NV12) {
		ApplyTable(*chromaSource, dst->data[1], static_cast<size_t>(dst->linesize[1]), tables.chroma.ptr<uint8_t>());
	} else {
		m_Chroma.create(chromaSource->rows, chromaSource->cols, CV_8UC2);
		ApplyTable(*chromaSource, m_Chroma.ptr<uint8_t>(), m_Chroma.step[0], tables.chroma.ptr<uint8_t>());
		cv::Mat dstU(dst->height / 2, dst->width / 2, CV_8UC1, dst->data[1], static_cast<size_t>(dst->linesize[1]));
		cv::Mat dstV(dst->height / 2, dst->width / 2, CV_8UC1, dst->data[2], static_cast<size_t>(dst->linesize[2]));
		cv::extractChannel(m_Chroma, dstU, 0);
		cv::extractChannel(m_Chroma, dstV, 1);
	}
	return true;
}

#endif
