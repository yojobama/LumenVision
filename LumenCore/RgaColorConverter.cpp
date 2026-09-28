#ifdef LUMEN_WITH_RGA
#include "RgaColorConverter.h"

// librga's headers need NULL defined before inclusion (they do not include <cstddef>); keep this include first.
#include <cstddef>
#include <im2d.h>

extern "C" {
#include <libavutil/imgutils.h>
}

bool RgaColorConverter::ConvertBgrToNv12(const cv::Mat& bgrFrame, AVFrame* dstFrame)
{
	int width = bgrFrame.cols;
	int height = bgrFrame.rows;

	size_t scratchSize = static_cast<size_t>(width) * height * 3 / 2;
	if (m_Scratch.size() != scratchSize) m_Scratch.resize(scratchSize);

	// wstride is in pixels, not bytes; bgrFrame.step is in bytes, so divide by elemSize() for
	// non-tightly-packed sources (e.g. ROI views).
	int srcWstride = static_cast<int>(bgrFrame.step / bgrFrame.elemSize());
	rga_buffer_t src = wrapbuffer_virtualaddr_t(
		const_cast<uchar*>(bgrFrame.data), width, height, srcWstride, height, RK_FORMAT_BGR_888);
	// tightly packed (wstride=width): the layout RGA produces for a planar destination given one vir_addr,
	// not necessarily dstFrame's linesize[0]/[1]
	rga_buffer_t dst = wrapbuffer_virtualaddr_t(
		m_Scratch.data(), width, height, width, height, RK_FORMAT_YCbCr_420_SP);

	// BT601 limited range, matching libswscale's default so switching between RGA and sws_scale does not shift colours
	IM_STATUS status = imcvtcolor(src, dst, RK_FORMAT_BGR_888, RK_FORMAT_YCbCr_420_SP, IM_RGB_TO_YUV_BT601_LIMIT);
	if (status != IM_STATUS_SUCCESS) return false;

	av_image_copy_plane(dstFrame->data[0], dstFrame->linesize[0], m_Scratch.data(), width, width, height);
	av_image_copy_plane(dstFrame->data[1], dstFrame->linesize[1], m_Scratch.data() + static_cast<size_t>(width) * height, width, width, height / 2);
	return true;
}

#endif // LUMEN_WITH_RGA
