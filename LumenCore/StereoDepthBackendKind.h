#pragma once

// Unscoped enum: Manager.h exposes it via SWIG, which wraps a plain enum as a C# enum but a scoped
// `enum class` as an opaque SWIGTYPE_p_* handle.
enum StereoDepthBackendKind {
	STEREO_BACKEND_CODEC_AUTO,          // cs_init() auto-probe
	STEREO_BACKEND_CODEC_LAVC,          // codec-stereo backend_override = "lavc_sw"
	STEREO_BACKEND_CODEC_RKMPP_HWENC,   // codec-stereo backend_override = "rkmpp_hwenc"
	STEREO_BACKEND_SGBM                 // cv::StereoSGBM, block-aggregated to the same grid
};
