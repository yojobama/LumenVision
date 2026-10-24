#pragma once
#ifdef LUMEN_WITH_RGA

#include "Frame.h"
#include <opencv2/opencv.hpp>
#include <cstdint>

// Prepares a detector's input on the RK3588's RGA: the frame (a camera's NV12 picture or a BGR one) is scaled and colour-converted in one hardware pass
// into its place in an RGB canvas, the rest of which is painted the YOLO letterbox grey. Run() reports failure rather than throwing, and the
// caller then prepares the input on the CPU.
class RgaLetterbox {
public:
	// `canvas` is canvasWidth x canvasHeight x 3 bytes, tightly packed RGB; `picture` is where the scaled frame goes in it.
	bool Run(const Frame& frame, uint8_t* canvas, int canvasWidth, int canvasHeight, const cv::Rect& picture);

private:
	// what was last painted grey: the border only needs painting again when the canvas or the picture's place in it changes
	const uint8_t* m_PaintedCanvas = nullptr;
	int m_PaintedWidth = 0, m_PaintedHeight = 0;
	cv::Rect m_PaintedPicture;
};

#endif // LUMEN_WITH_RGA
