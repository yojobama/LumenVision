#pragma once
#ifdef LUMEN_WITH_VULKAN_APRILTAG

#include "IApriltagBackend.h"
#include "ApriltagFamily.h"
#include <vkapriltag/Detector.h>
#include <atomic>
#include <stdexcept>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

// GPU (Vulkan compute) AprilTag detection via the vkapriltag submodule (third_party/vkapriltag), through its public Detector (vkapriltag 2.0).
//
// The library no longer uses libapriltag's types; Detect() converts each of its detections into an apriltag_detection_t (with a libapriltag family
// and a 3x3 homography) owned by this backend, so pose estimation, JSON and annotation stay backend-independent (the CPU backend and libapriltag stay for the
// CPU path). Poses come from the library's detector (SolvesPoses/ConfigurePose/GetPose): it is rebuilt when the intrinsics or tag size change.
class VkApriltagBackend : public IApriltagBackend {
public:
	// Thrown when the GPU is fine but this frame size cannot be run on it (too small to hold a tag, past the pipeline's limits, ...): the detector
	// then uses the CPU backend for that size only and tries the GPU again for the next one. Any other exception from the constructor means
	// there is no usable device (no driver, no Vulkan), which is permanent.
	struct UnsupportedFrameSize : std::runtime_error {
		using std::runtime_error::runtime_error;
	};

	// throws (via vk::Context's constructor / CheckVk) if no usable Vulkan compute device is found or the frame size is
	// unusable; callers should catch this and fall back to CpuApriltagBackend.
	// frameWidth/frameHeight must be the real frame size: the GPU buffers and decimation are fixed at construction.
	// See ApriltagTuning for defaults; decimation is rounded to an integer the frame size divides by (see .cpp).
	VkApriltagBackend(int frameWidth, int frameHeight, ApriltagTuning tuning = ApriltagTuning());
	~VkApriltagBackend() override;

	zarray_t* Detect(const cv::Mat& grayFrame) override;
	void ReleaseResult(zarray_t* detections) override; // no-op: the backend owns the list until the next Detect()
	std::string Name() const override { return "Vulkan (vkapriltag)"; }

	// the CPU-tail thread count the detector was built with (the analogue of CpuApriltagBackend's nthreads)
	int GetThreads() const override { return static_cast<int>(m_Threads); }
	// the integer decimation actually baked into this GPU pipeline (may differ from the request)
	float GetQuadDecimate() const override { return static_cast<float>(m_Decimation); }
	bool GetQuadDecimateSupported() const override { return true; }
	bool GetRefineEdges() const override { return m_RefineEdges; }
	// vkapriltag 2.0 has the one profile-fit refinement; the requested mode cannot change it
	RefineEdgesMode GetRefineMode() const override { return REFINE_PROFILE; }
	bool GetRefineModeSupported() const override { return false; }
	ApriltagFamilyKind GetFamily() const override { return m_FamilyKind; }
	// the GPU pipeline has no blur stage
	float GetQuadSigma() const override { return 0.0f; }
	bool GetQuadSigmaSupported() const override { return false; }
	int GetMaxHamming() const override { return m_MaxHamming; }
	std::string GetGpuDeviceName() const override { return m_GpuDeviceName; }
	// the library's gate, so an APRILTAG_VK_PROBE_MARGIN override shows
	float GetProbeMargin() const override { return m_ProbeMargin; }
	void LastStageTimingsUs(std::vector<std::pair<std::string, double>>& out) const override { out = m_LastStageUs; }
	bool SolvesPoses() const override { return true; }
	void ConfigurePose(double fx, double fy, double cx, double cy, double tagSize) override;
	bool GetPose(int index, double* R, double* t, double& error) const override;
	std::string GetIngestMode() const override { return m_LastFrameImported.load() ? "host-import" : "copy"; }

	// the names of the Vulkan devices a sink can run on, in vkEnumeratePhysicalDevices order (the index ApriltagTuning::gpuDevice takes);
	// empty when there is no usable Vulkan driver
	static std::vector<std::string> ListDevices();


	int GetFrameWidth() const { return m_FrameWidth; }
	int GetFrameHeight() const { return m_FrameHeight; }

	// The integer decimation the GPU pipeline can use for a requested value on a given frame size: rounded, at least 1,
	// stepped down until both dimensions divide evenly. Public + static so it is unit-testable without a GPU.
	static uint32_t ResolveDecimation(float requested, int frameWidth, int frameHeight);

private:
	// Registers `gray` with the GPU detector for in-place reads when it is a whole pooled (page-aligned) frame; true when the
	// detector will read it without an upload copy. False for any other frame, which Detect() uploads as before.
	bool ImportFrameIfPossible(const cv::Mat& gray);

	// Releases everything the constructor built, in dependency order; shared by the destructor and the constructor's failure path (a destructor
	// does not run for an object whose constructor threw, which left the detector and the family leaking when no Vulkan device was found).
	void ReleaseResources();

	// One Vulkan device per GPU is shared by every Vulkan AprilTag sink; each sink's detector gets its own submission lane (its stream).
	struct SharedDevice {
		std::shared_ptr<apriltag_vulkan::Device> device;
		uint32_t stream = 0; // this sink's index among the device's streams
	};
	static SharedDevice AcquireDevice(int deviceIndex);
public:
	// how many Vulkan contexts are alive (one per distinct GPU in use); for tests
	static size_t LiveContextCount();
private:

	// each frame is a different pooled buffer for a while, so the import cache is capped; frames beyond it are copied
	static constexpr size_t kMaxImportedFrames = 32;
	// the smallest decimated side the GPU pipeline is built for (a tag cannot be found in less)
	static constexpr int kMinDecimatedSide = 8;

	apriltag_family_t* m_Family; // libapriltag's table for the detections' `family` field (the GPU detector has its own)
	ApriltagFamilyKind m_FamilyKind = APRILTAG_FAMILY_36H11;
	int m_MaxHamming = 2;
	std::string m_GpuDeviceName;
	int m_FrameWidth = 0;
	int m_FrameHeight = 0;
	// The GPU pipeline sizes its grey buffer as width*height/4 words, so a frame whose pixel count is not a multiple of four cannot be uploaded as it is.
	// Those frames go to the GPU on a copy widened (by repeating the last column) to a multiple of four; the tags' pixel coordinates are unchanged
	// because the extra columns are on the right. m_GpuWidth is that width (the frame's own when no padding is needed).
	int m_GpuWidth = 0;
	cv::Mat m_PaddedFrame;
	std::shared_ptr<void> m_PaddedFrameOwner;
	uint32_t m_Decimation = 2;
	RefineEdgesMode m_RefineMode = REFINE_EXACT;

	bool m_HostImportAvailable = false;
	std::unordered_set<const uint8_t*> m_ImportedFrames;
	std::atomic<bool> m_LastFrameImported{ false };
	std::vector<std::pair<std::string, double>> m_LastStageUs; // written by Detect(), read by the same thread right after
	std::shared_ptr<void> m_PackedFrameOwner; // keeps the repacked copy of the last non-contiguous frame alive while the GPU reads it

	// what the detector was built with, kept to rebuild it when the pose configuration changes (ConfigurePose)
	apriltag_vulkan::DetectorOptions m_Options;
	double m_PoseConfig[5] = {};
	std::vector<apriltag_vulkan::TagPose> m_Poses; // aligned with m_Result
	void BuildDetector();
	bool m_RefineEdges = true;
	uint32_t m_Threads = 4;
	float m_ProbeMargin = 0.0f;

	// the list Detect() returns: libapriltag-typed copies of the library's detections, replaced on the next call
	void ClearResult();
	std::vector<apriltag_detection_t*> m_Result;
	zarray_t* m_ResultList = nullptr;

	// declaration order is destruction order in reverse: the detector, then the device it runs on
	std::shared_ptr<apriltag_vulkan::Device> m_Device;
	std::unique_ptr<apriltag_vulkan::Detector> m_VkDetector;
};

#endif // LUMEN_WITH_VULKAN_APRILTAG
