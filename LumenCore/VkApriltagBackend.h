#pragma once
#ifdef LUMEN_WITH_VULKAN_APRILTAG

#include "IApriltagBackend.h"
#include "ApriltagFamily.h"
#include <vkapriltag/TagDecoder.h>
#include <vkapriltag/gpu/GpuDetector.h>
#include <vkapriltag/gpu/QuadDecode.h>
#include <vkapriltag/vk/Context.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

// GPU (Vulkan compute) AprilTag detection via the vkapriltag submodule (third_party/vkapriltag).
//
// Requires the patched apriltag build (v3.4.5 plus exported quad_decode_index/reconcile_detections/quad_sample_code/apriltag_min_hamming; see
// third_party/vkapriltag/apriltags_vulkan/cmake/patches/apriltag-expose-decode-steps.patch and apriltag-expose-code-probe.patch) as the only
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
	ApriltagFamilyKind GetFamily() const override { return m_FamilyKind; }
	// the GPU pipeline has no blur stage
	float GetQuadSigma() const override { return 0.0f; }
	bool GetQuadSigmaSupported() const override { return false; }
	int GetMaxHamming() const override { return m_MaxHamming; }
	std::string GetGpuDeviceName() const override { return m_GpuDeviceName; }
	// the library's gate, so an APRILTAG_VK_PROBE_MARGIN override shows
	float GetProbeMargin() const override { return m_TagDecoder->probe_margin(); }
	void LastStageTimingsUs(std::vector<std::pair<std::string, double>>& out) const override { out = m_LastStageUs; }
	std::string GetIngestMode() const override { return m_LastFrameImported.load() ? "host-import" : "copy"; }

	// the names of the Vulkan devices a sink can run on, in vkEnumeratePhysicalDevices order (the index ApriltagTuning::gpuDevice takes);
	// empty when there is no usable Vulkan driver
	static std::vector<std::string> ListDevices();

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
	// Registers `gray` with the GPU detector for in-place reads when it is a whole pooled (page-aligned) frame; true when the
	// detector will read it without an upload copy. False for any other frame, which Detect() uploads as before.
	bool ImportFrameIfPossible(const cv::Mat& gray);

	// One Vulkan context per GPU is shared by every Vulkan AprilTag sink in the process (one instance, device and pipeline cache
	// instead of one each), and each sink gets its own submission lane, spread over the device's compute queue families.
	struct SharedContext {
		std::shared_ptr<apriltag_vulkan::vk::Context> context;
		uint32_t stream = 0; // this sink's index among the context's streams
	};
	static SharedContext AcquireContext(int deviceIndex);
public:
	// how many Vulkan contexts are alive (one per distinct GPU in use); for tests
	static size_t LiveContextCount();
private:

	// each frame is a different pooled buffer for a while, so the import cache is capped; frames beyond it are copied
	static constexpr size_t kMaxImportedFrames = 32;

	apriltag_detector_t* m_Detector;
	apriltag_family_t* m_Family;
	ApriltagFamilyKind m_FamilyKind = APRILTAG_FAMILY_36H11;
	int m_MaxHamming = 2;
	std::string m_GpuDeviceName;
	int m_FrameWidth = 0;
	int m_FrameHeight = 0;
	uint32_t m_Decimation = 2;
	RefineEdgesMode m_RefineMode = REFINE_EXACT;

	bool m_HostImportAvailable = false;
	std::unordered_set<const uint8_t*> m_ImportedFrames;
	std::atomic<bool> m_LastFrameImported{ false };
	std::vector<std::pair<std::string, double>> m_LastStageUs; // written by Detect(), read by the same thread right after
	std::shared_ptr<void> m_PackedFrameOwner; // keeps the repacked copy of the last non-contiguous frame alive while the GPU reads it

	// declaration order is destruction order in reverse: the detector, then the lane it submits through, then the shared context
	std::shared_ptr<apriltag_vulkan::vk::Context> m_Context;
	std::unique_ptr<apriltag_vulkan::vk::Lane> m_Lane;
	std::unique_ptr<apriltag_vulkan::GpuDetector> m_GpuDetector;
	std::unique_ptr<apriltag_vulkan::QuadDecode> m_QuadDecode;
	std::unique_ptr<apriltag_vulkan::TagDecoder> m_TagDecoder;
};

#endif // LUMEN_WITH_VULKAN_APRILTAG
