#pragma once
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

// Shared invalid-cell marker for every backend's disparity output; matches codec-stereo's CS_DISPARITY_INVALID (cs.h)
// without depending on cs.h.
#define STEREO_DISPARITY_INVALID (-1.0f)

// Backend abstraction like IApriltagBackend/IDetectionBackend: every implementation returns disparity on the same block
// grid, so StereoDepthNode's rectification, JSON and annotation are backend-independent.
class IStereoDepthBackend {
public:
	virtual ~IStereoDepthBackend() = default;

	// rectLeft/rectRight must be rectified, single-channel (CV_8UC1), same size, and a size the backend's block size divides
	// evenly; StereoDepthNode crops to that (never pads: a synthetic border harms motion-vector backends).
	//
	// On success, fills disparityOut with cols*rows floats (block-raster order, CS_DISPARITY_
	// INVALID-equivalent for invalid cells - see cs.h) and sets cols/rows, and returns true.
	virtual bool Compute(const cv::Mat& rectLeft, const cv::Mat& rectRight,
		std::vector<float>& disparityOut, int& cols, int& rows) = 0;

	virtual std::string Name() const = 0;
	virtual int BlockW() const = 0;
	virtual int BlockH() const = 0;
};
