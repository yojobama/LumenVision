#ifdef LUMEN_WITH_VULKAN_APRILTAG
#include "VkApriltagBackend.h"
#include <cmath>
#include <map>
#include "FramePool.h"
#include <cstdlib>
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

RefineEdgesMethod VkApriltagBackend::ToRefineMethod(RefineEdgesMode mode)
{
	switch (mode) {
	case REFINE_UPSTREAM: return RefineEdgesMethod::kUpstream;
	case REFINE_FAST: return RefineEdgesMethod::kFast;
	case REFINE_ULTRAFAST: return RefineEdgesMethod::kUltraFast;
	case REFINE_EXACT:
	default: return RefineEdgesMethod::kExact;
	}
}

std::string VkApriltagBackend::RefineEnvOverride()
{
#ifdef _MSC_VER
	char* env = nullptr;
	size_t length = 0;
	std::string value;
	if (_dupenv_s(&env, &length, "APRILTAG_VK_REFINE") == 0 && env != nullptr) value = env;
	std::free(env);
	return value;
#else
	const char* env = std::getenv("APRILTAG_VK_REFINE");
	return env ? std::string(env) : std::string();
#endif
}

// LUMEN_APRILTAG_FRAME_IMPORT=0 uploads every frame, for comparing against the zero-copy path
static bool FrameImportDisabledByEnv()
{
#ifdef _MSC_VER
	char* env = nullptr;
	size_t length = 0;
	bool disabled = _dupenv_s(&env, &length, "LUMEN_APRILTAG_FRAME_IMPORT") == 0 && env != nullptr && std::string(env) == "0";
	std::free(env);
	return disabled;
#else
	const char* env = std::getenv("LUMEN_APRILTAG_FRAME_IMPORT");
	return env != nullptr && std::string(env) == "0";
#endif
}

namespace {
// the live shared contexts by requested device index (-1 = automatic); a context lives as long as some sink uses it
struct ContextEntry {
	std::weak_ptr<vk::Context> context;
	uint32_t nextStream = 0;
};
std::mutex g_ContextMutex;
std::map<int, ContextEntry> g_Contexts;

// Context::StreamSlot spreads this many streams over a device's compute families; more sinks than this just share queues
constexpr uint32_t kExpectedStreams = 4;
}

VkApriltagBackend::SharedContext VkApriltagBackend::AcquireContext(int deviceIndex)
{
	std::lock_guard<std::mutex> lock(g_ContextMutex);
	ContextEntry& entry = g_Contexts[deviceIndex];
	std::shared_ptr<vk::Context> context = entry.context.lock();
	if (!context) {
		vk::ContextOptions options;
		options.device_index = deviceIndex;
		options.expected_streams = kExpectedStreams;
		context = std::make_shared<vk::Context>(options); // throws without a usable device; nothing is cached then
		entry.context = context;
		entry.nextStream = 0;
	}
	return SharedContext{ context, entry.nextStream++ };
}

size_t VkApriltagBackend::LiveContextCount()
{
	std::lock_guard<std::mutex> lock(g_ContextMutex);
	size_t live = 0;
	for (const auto& entry : g_Contexts) if (!entry.second.context.expired()) live++;
	return live;
}

std::vector<std::string> VkApriltagBackend::ListDevices()
{
	std::vector<std::string> names;
	try {
		for (const vk::DeviceCaps& caps : vk::Context::EnumerateDevices()) names.push_back(caps.name);
	} catch (const std::exception&) {
		// no Vulkan loader or driver: no devices to choose from
	}
	return names;
}

VkApriltagBackend::VkApriltagBackend(int frameWidth, int frameHeight, ApriltagTuning tuning)
	: m_FrameWidth(frameWidth), m_FrameHeight(frameHeight), m_RefineMode(tuning.refineMode)
{
	if (frameWidth <= 0 || frameHeight <= 0) {
		// the GPU pipeline's buffers are sized from these - there is no valid default
		throw std::invalid_argument("VkApriltagBackend needs the real frame size (got " +
			std::to_string(frameWidth) + "x" + std::to_string(frameHeight) + ")");
	}

	m_FamilyKind = tuning.family;
	m_MaxHamming = ClampMaxHamming(tuning.maxHamming);
	m_Family = CreateApriltagFamily(m_FamilyKind);
	m_Detector = apriltag_detector_create();
	apriltag_detector_add_family_bits(m_Detector, m_Family, m_MaxHamming);
	// set explicitly rather than left to apriltag_detector_create(), whose refine_edges default (TRUE) differs from
	// TagDecoder's documented default (false)
	m_Detector->refine_edges = tuning.refineEdges;

	m_Decimation = ResolveDecimation(tuning.quadDecimate, frameWidth, frameHeight);

	SharedContext shared = AcquireContext(tuning.gpuDevice);
	m_Context = shared.context;
	m_Lane = m_Context->CreateLane(m_Context->StreamSlot(shared.stream));
	m_GpuDeviceName = m_Context->caps().name;
	// importing needs the extension and an alignment our pool blocks satisfy
	const vk::DeviceCaps& caps = m_Context->caps();
	m_HostImportAvailable = !FrameImportDisabledByEnv() && caps.has_external_memory_host &&
		caps.min_host_pointer_alignment > 0 && FramePool::kBlockAlignment % caps.min_host_pointer_alignment == 0;

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

	m_GpuDetector = std::make_unique<GpuDetector>(*m_Context, *m_Lane, config);
	m_QuadDecode = std::make_unique<QuadDecode>(config);
	// TagDecoder must be told the same decimation as the GPU pass: refine_edges derives its per-edge search radius from it.
	// APRILTAG_VK_REFINE, if set, overrides the requested method inside the library.
	m_TagDecoder = std::make_unique<TagDecoder>(m_Detector, m_Decimation, config.cpu_threads,
		ToRefineMethod(tuning.refineMode), tuning.probeMargin > 0.0f ? tuning.probeMargin : 0.0f);
}

VkApriltagBackend::~VkApriltagBackend()
{
	// destroy in reverse dependency order before m_Context (owns the device the others hold
	// live handles into) and before the detector/family the TagDecoder borrows a pointer to
	m_TagDecoder.reset();
	m_QuadDecode.reset();
	m_GpuDetector.reset();
	m_Lane.reset();
	m_Context.reset();

	apriltag_detector_destroy(m_Detector);
	DestroyApriltagFamily(m_FamilyKind, m_Family);
}

bool VkApriltagBackend::ImportFrameIfPossible(const cv::Mat& gray)
{
	if (!m_HostImportAvailable || !gray.isContinuous()) return false;
	const uint8_t* data = gray.data;
	if (m_ImportedFrames.count(data) != 0) return true;
	if (m_ImportedFrames.size() >= kMaxImportedFrames) return false;

	// only a buffer from the pool is known to be aligned, sized in whole pages and kept alive for the process
	const size_t bytes = FramePool::Instance().BlockCapacity(data);
	if (bytes < gray.total() * gray.elemSize()) return false;
	try {
		m_GpuDetector->ImportHostFrame(data, bytes);
	} catch (const std::exception&) {
		// the driver refused this memory: stop trying and keep uploading
		m_HostImportAvailable = false;
		return false;
	}
	m_ImportedFrames.insert(data);
	return true;
}

zarray_t* VkApriltagBackend::Detect(const cv::Mat& frame)
{
	// The GPU reads the frame as width*height packed bytes. A crop of a larger frame (a half of a side-by-side camera) is a view with the
	// parent's row stride, so it is repacked first; anything else would have the GPU read pixels from the wrong rows.
	cv::Mat packed;
	if (!frame.isContinuous()) {
		std::shared_ptr<void> owner;
		packed = FramePool::Instance().Acquire(frame.rows, frame.cols, frame.type(), owner);
		frame.copyTo(packed);
		m_PackedFrameOwner = owner;
	}
	const cv::Mat& grayFrame = frame.isContinuous() ? frame : packed;

	m_LastFrameImported = ImportFrameIfPossible(grayFrame);
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
