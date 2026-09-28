#ifdef LUMEN_WITH_VULKAN_APRILTAG
#include "VkApriltagBackend.h"
#include <apriltag/tag36h11.h>
#include <cmath>
#include <stdexcept>
#include <string>

using namespace apriltag_vulkan;

uint32_t VkApriltagBackend::ResolveDecimation(float requested, int frameWidth, int frameHeight)
{
	// <= 0: DetectorConfig's default (2). The GPU pipeline only supports integer decimation (one representative
	// pixel per NxN block), so a fractional request rounds.
	long d = requested > 0.0f ? std::lround(requested) : 2;
	if (d < 1) d = 1;
	while (d > 1 && (frameWidth % d != 0 || frameHeight % d != 0)) d--;
	return static_cast<uint32_t>(d);
}

VkApriltagBackend::VkApriltagBackend(int frameWidth, int frameHeight, ApriltagTuning tuning)
	: m_FrameWidth(frameWidth), m_FrameHeight(frameHeight)
{
	if (frameWidth <= 0 || frameHeight <= 0) {
		// the GPU pipeline's buffers are sized from these - there is no valid default
		throw std::invalid_argument("VkApriltagBackend needs the real frame size (got " +
			std::to_string(frameWidth) + "x" + std::to_string(frameHeight) + ")");
	}

	m_Family = tag36h11_create();
	m_Detector = apriltag_detector_create();
	apriltag_detector_add_family(m_Detector, m_Family);
	// set explicitly rather than left to apriltag_detector_create(), whose refine_edges default (TRUE) differs from
	// TagDecoder's documented default (false)
	m_Detector->refine_edges = tuning.refineEdges;

	m_Decimation = ResolveDecimation(tuning.quadDecimate, frameWidth, frameHeight);

	m_Context = std::make_unique<vk::Context>();

	DetectorConfig config;
	config.width = static_cast<uint32_t>(frameWidth);
	config.height = static_cast<uint32_t>(frameHeight);
	config.decimation = m_Decimation;
	config.tag_width = static_cast<uint32_t>(m_Family->width_at_border);
	config.reversed_border = m_Family->reversed_border;
	config.normal_border = !m_Family->reversed_border;
	// 4 threads by default rather than hardware_concurrency() (GPU-bound with a small CPU tail); QuadDecode's pool is
	// fixed at construction, so changing this rebuilds the backend. Overridable per sink via ApriltagTuning.nthreads.
	config.cpu_threads = tuning.nthreads > 0 ? static_cast<uint32_t>(tuning.nthreads) : 4;

	m_GpuDetector = std::make_unique<GpuDetector>(*m_Context, config);
	m_QuadDecode = std::make_unique<QuadDecode>(config);
	// TagDecoder must be told the same decimation as the GPU pass: refine_edges derives its per-edge search radius from it.
	// kExact matches upstream's refine_edges without libm modf(); APRILTAG_VK_REFINE overrides it at runtime.
	m_TagDecoder = std::make_unique<TagDecoder>(m_Detector, m_Decimation, config.cpu_threads,
		RefineEdgesMethod::kExact);
}

VkApriltagBackend::~VkApriltagBackend()
{
	// destroy in reverse dependency order before m_Context (owns the device the others hold
	// live handles into) and before the detector/family the TagDecoder borrows a pointer to
	m_TagDecoder.reset();
	m_QuadDecode.reset();
	m_GpuDetector.reset();
	m_Context.reset();

	apriltag_detector_destroy(m_Detector);
	tag36h11_destroy(m_Family);
}

zarray_t* VkApriltagBackend::Detect(const cv::Mat& grayFrame)
{
	m_GpuDetector->Detect(grayFrame.data);

	std::vector<DetectedQuad> quads = m_QuadDecode->Decode(m_GpuDetector->last_line_fit_points);

	return m_TagDecoder->Decode(quads, grayFrame.data,
		static_cast<uint32_t>(grayFrame.cols), static_cast<uint32_t>(grayFrame.rows),
		m_Family->reversed_border);
}

void VkApriltagBackend::ReleaseResult(zarray_t* /*detections*/)
{
	// intentionally empty - see header
}

#endif // LUMEN_WITH_VULKAN_APRILTAG
