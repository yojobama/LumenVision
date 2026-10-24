#ifdef LUMEN_WITH_RGA
#include "RgaLetterbox.h"

// librga's headers need NULL defined before inclusion (they do not include <cstddef>); keep this include first.
#include <cstddef>
#include <im2d.h>

bool RgaLetterbox::Run(const Frame& frame, uint8_t* canvas, int canvasWidth, int canvasHeight, const cv::Rect& picture)
{
	if (frame.empty() || !canvas || picture.width <= 0 || picture.height <= 0) return false;
	// RGA works on even rectangles
	if (picture.x % 2 != 0 || picture.y % 2 != 0 || picture.width % 2 != 0 || picture.height % 2 != 0) return false;

	rga_buffer_t src;
	int srcWidth = 0, srcHeight = 0;
	if (frame.IsNv12()) {
		const cv::Mat& y = frame.YPlane();
		const cv::Mat& uv = frame.UvPlane();
		// librga takes one address and finds the chroma plane after wstride x hstride luma rows, so the planes must sit in one buffer that way
		const ptrdiff_t gap = uv.data - y.data;
		if (y.step != uv.step || gap <= 0 || gap % y.step != 0) return false;
		const int hstride = static_cast<int>(gap / y.step);
		if (hstride < y.rows) return false;
		srcWidth = y.cols;
		srcHeight = y.rows;
		src = wrapbuffer_virtualaddr_t(const_cast<uchar*>(y.data), srcWidth, srcHeight, static_cast<int>(y.step), hstride, RK_FORMAT_YCbCr_420_SP);
	} else {
		const cv::Mat& bgr = frame.AsBgr();
		srcWidth = bgr.cols;
		srcHeight = bgr.rows;
		src = wrapbuffer_virtualaddr_t(const_cast<uchar*>(bgr.data), srcWidth, srcHeight, static_cast<int>(bgr.step / bgr.elemSize()), srcHeight, RK_FORMAT_BGR_888);
	}
	rga_buffer_t dst = wrapbuffer_virtualaddr_t(canvas, canvasWidth, canvasHeight, canvasWidth, canvasHeight, RK_FORMAT_RGB_888);

	if (m_PaintedCanvas != canvas || m_PaintedWidth != canvasWidth || m_PaintedHeight != canvasHeight || m_PaintedPicture != picture) {
		const im_rect whole = { 0, 0, canvasWidth, canvasHeight };
		if (imfill(dst, whole, 0xff727272) != IM_STATUS_SUCCESS) return false; // YOLO's 114 grey
		m_PaintedCanvas = canvas;
		m_PaintedWidth = canvasWidth;
		m_PaintedHeight = canvasHeight;
		m_PaintedPicture = picture;
	}

	rga_buffer_t pattern{};
	const im_rect sourceRect = { 0, 0, srcWidth, srcHeight };
	const im_rect targetRect = { picture.x, picture.y, picture.width, picture.height };
	const im_rect patternRect = { 0, 0, 0, 0 };
	return improcess(src, dst, pattern, sourceRect, targetRect, patternRect, IM_SYNC) == IM_STATUS_SUCCESS;
}

#endif // LUMEN_WITH_RGA
