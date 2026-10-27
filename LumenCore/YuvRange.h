#pragma once

#include <opencv2/core.hpp>
#include <cstddef>
#include <cstdint>

// The BT.601 range squeeze, full (JPEG) range to video (limited) range: luma 0-255 to 16-235, chroma 0-255 to 16-240 around 128.
// The cameras' NV12 frames come from the hardware JPEG decoder, so they hold full-range values, while the H.264 encoders and OpenCV's
// NV12 conversions expect video range; both go through these tables (NEON table lookups on ARM64).
namespace YuvRange {

// the tables: entry v is the video-range value of full-range v
const uint8_t* LumaTable();
const uint8_t* ChromaTable();

// Applies a 256-entry table to every byte of `src`, row by row, into `dst` (dstStep bytes per row; padding between rows is never touched).
// A null table copies the rows.
void ApplyTable(const cv::Mat& src, uint8_t* dst, size_t dstStep, const uint8_t* table);

// A full-range NV12 picture (luma `y`, interleaved chroma `uv`) as BGR in `bgr` (reallocated unless it is already CV_8UC3 of the right size).
// OpenCV's NV12 conversion assumes video range (Y 16 is black, 235 white), so the planes are squeezed into scratch planes first; the result is
// a JPEG decoder's full-range conversion to within one level. Converting the full-range planes directly stretched every picture's contrast.
void FullRangeNv12ToBgr(const cv::Mat& y, const cv::Mat& uv, cv::Mat& bgr);

}
