#pragma once
#include <apriltag/apriltag.h>
#include <opencv2/opencv.hpp>

// Plain (unscoped) enum: SWIG wraps a scoped enum as an opaque handle (as with YoloVariant). Kept here so the CPU
// backend needn't include the Vulkan one.
enum ApriltagBackendKind {
	APRILTAG_BACKEND_CPU,
	APRILTAG_BACKEND_VULKAN
};

// Edge-refinement implementation (vkapriltag's RefineEdgesMethod). Plain (unscoped) enum for SWIG, as above. Only the Vulkan backend
// honours it; the CPU backend always runs upstream's refine_edges.
//   REFINE_UPSTREAM  upstream's compiled refine_edges() (the reference)
//   REFINE_EXACT     upstream's arithmetic without libm modf(); bit-identical to REFINE_UPSTREAM
//   REFINE_FAST      single-precision inner loop; not bit-identical
//   REFINE_ULTRAFAST as REFINE_FAST, and only quads that already decode unrefined are refined
enum RefineEdgesMode {
	REFINE_UPSTREAM,
	REFINE_EXACT,
	REFINE_FAST,
	REFINE_ULTRAFAST
};

// The runtime-tunable detector knobs, shared by both backends so a sink can be rebuilt (backend
// switch, frame-size change) with exactly the same settings.
//   nthreads     <= 0: the backend's own default (CPU: apriltag's 1; Vulkan: hardware_concurrency)
//   quadDecimate <= 0: the backend's own default (2 for both). Vulkan only supports integers and
//                      needs the frame to be divisible by it - see VkApriltagBackend.
//   refineEdges:       libapriltag's refine_edges (gradient-based corner refinement). Default true; offsets
//                      decimation's coarser quads.
//   refineMode:        which implementation refineEdges uses (see RefineEdgesMode); ignored when refineEdges is false or on the CPU backend.
struct ApriltagTuning {
	int nthreads = 0;
	float quadDecimate = 0.0f;
	bool refineEdges = true;
	RefineEdgesMode refineMode = REFINE_EXACT;
};

// Detection-only backend abstraction: both implementations return a zarray_t* of apriltag_detection_t* from the same
// apriltag library, so pose estimation, JSON and annotation are backend-independent.
class IApriltagBackend {
public:
	virtual ~IApriltagBackend() = default;

	// returns a zarray_t* of apriltag_detection_t*, owned by the backend and valid until the next Detect() or destruction
	virtual zarray_t* Detect(const cv::Mat& grayFrame) = 0;

	// releases a result previously returned by Detect() - CPU uses apriltag_detections_destroy,
	// Vulkan's TagDecoder owns its own zarray_t internally and this is a no-op for it
	virtual void ReleaseResult(zarray_t* detections) = 0;

	virtual std::string Name() const = 0;

	// Runtime tuning knobs, reporting what is actually in effect rather than what was requested. CpuApriltagBackend backs
	// all three directly; VkApriltagBackend maps GetThreads() to cpu_threads and fixes its integer decimation at construction.
	// GetQuadDecimateSupported() says whether a control would do anything.
	virtual int GetThreads() const = 0;
	virtual float GetQuadDecimate() const = 0;
	virtual bool GetQuadDecimateSupported() const = 0;
	virtual bool GetRefineEdges() const = 0;
	// the refine implementation in effect, and whether the backend lets it be chosen (false: CPU always runs REFINE_UPSTREAM)
	virtual RefineEdgesMode GetRefineMode() const = 0;
	virtual bool GetRefineModeSupported() const = 0;
};
