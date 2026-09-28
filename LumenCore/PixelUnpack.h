#pragma once
#include <opencv2/core.hpp>
#include <cstddef>
#include <cstdint>

// Raw-sensor mono formats (V4L2 Y10/Y16/Y10P/Y10BPACK) -> 8-bit grayscale, keeping the most significant 8 bits.
//
// Free of <linux/videodev2.h> so it builds on every platform. `stride` is the driver's bytesperline (may exceed
// width * bytes-per-pixel); `dst` is (re)created as CV_8UC1 width x height, or written in place if already that size.
namespace PixelUnpack
{
	// V4L2_PIX_FMT_Y10: one little-endian 16-bit word per pixel, value in the low 10 bits.
	void Y10ToGray8(const uint8_t* data, int width, int height, size_t stride, cv::Mat& dst);

	// V4L2_PIX_FMT_Y16: one little-endian 16-bit word per pixel, full 16-bit range.
	void Y16ToGray8(const uint8_t* data, int width, int height, size_t stride, cv::Mat& dst);

	// V4L2_PIX_FMT_Y10P (MIPI CSI-2 RAW10 packing - the Raspberry Pi/unicam OV9281 format): every
	// 4 pixels are 5 bytes, the first 4 holding each pixel's 8 MSBs and the 5th their 2 LSBs.
	void Y10PToGray8(const uint8_t* data, int width, int height, size_t stride, cv::Mat& dst);

	// V4L2_PIX_FMT_Y10BPACK: a big-endian 10-bit bit stream - 4 pixels in 5 bytes, but each
	// pixel's bits run straight across byte boundaries (unlike Y10P's MSB bytes + LSB byte).
	void Y10BPackToGray8(const uint8_t* data, int width, int height, size_t stride, cv::Mat& dst);

	// V4L2_PIX_FMT_GREY with arbitrary stride - a plain copy, row by row.
	void Gray8Copy(const uint8_t* data, int width, int height, size_t stride, cv::Mat& dst);

	// The minimum bytesperline for `width` pixels in each format - what the V4L2 spec says a
	// driver must report at least, used to reject a truncated buffer before reading past it.
	size_t MinStrideY10(int width);  // also Y16
	size_t MinStrideY10Packed(int width); // Y10P and Y10BPACK
}
