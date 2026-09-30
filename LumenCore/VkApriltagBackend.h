#pragma once
#ifdef LUMEN_WITH_VULKAN_APRILTAG

#include "IApriltagBackend.h"
#include <vkapriltag/TagDecoder.h>
#include <vkapriltag/gpu/GpuDetector.h>
#include <vkapriltag/gpu/QuadDecode.h>
#include <vkapriltag/vk/Context.h>
#include <memory>

// GPU (Vulkan compute) AprilTag detection via the vkapriltag submodule (third_party/vkapriltag).
//
// Requires the patched apriltag build (v3.4.5 plus exported quad_decode_index/reconcile_detections; see
// third_party/vkapriltag/apriltags_vulkan/cmake/patches/apriltag-expose-decode-steps.patch) as the only
// libapriltag.so.3 (same SONAME as vanilla); install-deps.sh build_apriltag() installs it.
//
// TagDecoder::Decode() returns the same zarray_t* of apriltag_detection_t* as apriltag_detector_detect(), stopping at
// 2D detection (pose, JSON and annotation live in ApriltagDetector).
class VkApriltagBackend : public IApriltagBackend {
public:
	// throws (via vk::Context's constructor / CheckVk) if no usable Vulkan compute device is found or the frame size is
	// unusable; callers should catch this and fall back to CpuApriltagBackend.
	// frameWidth/frameHeight must be the real frame size: the GPU buffers and decimation are fixed at construction.
	// See ApriltagTuning for defaults; decimation is rounded to an integer the frame size divides by (see .cpp).
	VkApriltagBackend(int frameWidth, int frameHeight, ApriltagTuning tuning = ApriltagTuning());
	~VkApriltagBackend() override;

	zarray_t* Detect(const cv::Mat& grayFrame) override;
	void ReleaseResult(zarray_t* detections) override; // no-op: TagDecoder owns its zarray_t
	std::string Name() const override { return "Vulkan (vkapriltag)"; }

	// QuadDecode's cpu_threads getter (the CPU-tail worker pool; the analogue of CpuApriltagBackend's nthreads)
	int GetThreads() const override { return static_cast<int>(m_QuadDecode->threads()); }
	// the integer decimation actually baked into this GPU pipeline (may differ from the request)
	float GetQuadDecimate() const override { return static_cast<float>(m_Decimation); }
	bool GetQuadDecimateSupported() const override { return true; }
	bool GetRefineEdges() const override { return m_Detector->refine_edges; }
	RefineEdgesMode GetRefineMode() const override { return m_RefineMode; }
	bool GetRefineModeSupported() const override { return true; }

	// vkapriltag's RefineEdgesMethod for a mode
	static apriltag_vulkan::RefineEdgesMethod ToRefineMethod(RefineEdgesMode mode);
	// the APRILTAG_VK_REFINE environment override ("" when unset); the library lets it win over the requested mode
	static std::string RefineEnvOverride();

	int GetFrameWidth() const { return m_FrameWidth; }
	int GetFrameHeight() const { return m_FrameHeight; }

	// The integer decimation the GPU pipeline can use for a requested value on a given frame size: rounded, at least 1,
	// stepped down until both dimensions divide evenly. Public + static so it is unit-testable without a GPU.
	static uint32_t ResolveDecimation(float requested, int frameWidth, int frameHeight);

private:
	apriltag_detector_t* m_Detector;
	apriltag_family_t* m_Family;
	int m_FrameWidth = 0;
	int m_FrameHeight = 0;
	uint32_t m_Decimation = 2;
	RefineEdgesMode m_RefineMode = REFINE_EXACT;

	std::unique_ptr<apriltag_vulkan::vk::Context> m_Context;
	std::unique_ptr<apriltag_vulkan::GpuDetector> m_GpuDetector;
	std::unique_ptr<apriltag_vulkan::QuadDecode> m_QuadDecode;
	std::unique_ptr<apriltag_vulkan::TagDecoder> m_TagDecoder;
};

#endif // LUMEN_WITH_VULKAN_APRILTAG
