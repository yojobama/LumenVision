#pragma once
#ifdef LUMEN_WITH_RGA

#include <opencv2/opencv.hpp>
#include <vector>

extern "C" {
#include <libavutil/frame.h>
}

// Offloads WebRTCSink's BGR24->NV12 colour conversion to the RK3588's RGA via librga's im2d C API.
//
// rga_buffer_t takes one virtual address per call (later planes derive from wstride*hstride), but AVFrame NV12 planes
// may be laid out differently, so output goes to a packed scratch buffer and each plane is copied via av_image_copy_plane.
class RgaColorConverter {
public:
	// Converts bgrFrame (CV_8UC3, BGR) into dstFrame, pre-allocated as AV_PIX_FMT_NV12 at the same size.
	// Returns false on any RGA failure; the caller falls back to sws_scale.
	bool ConvertBgrToNv12(const cv::Mat& bgrFrame, AVFrame* dstFrame);

private:
	// scratch buffer, reused across calls; reallocated only when the frame size changes
	std::vector<uint8_t> m_Scratch;
};

#endif // LUMEN_WITH_RGA
