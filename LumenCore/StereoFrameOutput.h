#pragma once

// Plain (unscoped) enum for the same SWIG reason as StereoDepthBackendKind.h.
enum StereoFrameOutput {
	STEREO_FRAME_DEPTH_COLORMAP,  // cv::applyColorMap over the normalised block-grid disparity
	STEREO_FRAME_RECTIFIED_LEFT,  // rectified left eye, undistorted - bind a downstream detector
	                              // to this (not the raw camera) so its bbox pixel coordinates
	                              // actually index into the disparity grid; see DepthFusionNode
	STEREO_FRAME_DEPTH_OVERLAY    // rectified left eye with the colormap alpha-blended over it
};
