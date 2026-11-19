#ifdef LUMEN_WITH_VULKAN_APRILTAG
#include "VkApriltagBackend.h"
#include "CpuAffinity.h"
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <map>
#include "FramePool.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vulkan/vulkan.h>

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
// the live shared devices by requested device index (-1 = automatic); a device lives as long as some sink uses it
struct DeviceEntry {
	std::weak_ptr<Device> device;
	uint32_t nextStream = 0;
};
std::mutex g_DeviceMutex;
std::map<int, DeviceEntry> g_Devices;

// the library spreads this many streams over a device's compute queues; more sinks than this just share queues
constexpr uint32_t kExpectedStreams = 4;

// the first line of Device::Describe() is "apriltag_vulkan: using <name> (<type>, Vulkan x.y.z)"
std::string DeviceNameFrom(const std::string& description)
{
	std::string line = description.substr(0, description.find('\n'));
	const std::string prefix = "apriltag_vulkan: using ";
	if (line.compare(0, prefix.size(), prefix) == 0) line.erase(0, prefix.size());
	const size_t paren = line.rfind(" (");
	if (paren != std::string::npos) line.erase(paren);
	return line;
}

float EnvFloat(const char* name, float fallback)
{
	const char* text = std::getenv(name);
	if (!text || !*text) return fallback;
	char* end = nullptr;
	const float value = std::strtof(text, &end);
	return end && *end == '\0' ? value : fallback;
}

const char* FamilyName(ApriltagFamilyKind kind)
{
	switch (kind) {
	case APRILTAG_FAMILY_16H5: return "tag16h5";
	case APRILTAG_FAMILY_25H9: return "tag25h9";
	case APRILTAG_FAMILY_STANDARD41H12: return "tagStandard41h12";
	case APRILTAG_FAMILY_36H11:
	default: return "tag36h11";
	}
}
}

VkApriltagBackend::SharedDevice VkApriltagBackend::AcquireDevice(int deviceIndex)
{
	std::lock_guard<std::mutex> lock(g_DeviceMutex);
	DeviceEntry& entry = g_Devices[deviceIndex];
	std::shared_ptr<Device> device = entry.device.lock();
	if (!device) {
		DeviceOptions options;
		options.device_index = deviceIndex;
		options.expected_streams = kExpectedStreams;
		device = Device::Create(options); // throws without a usable device; nothing is cached then
		entry.device = device;
		entry.nextStream = 0;
	}
	return SharedDevice{ device, entry.nextStream++ };
}

size_t VkApriltagBackend::LiveContextCount()
{
	std::lock_guard<std::mutex> lock(g_DeviceMutex);
	size_t live = 0;
	for (const auto& entry : g_Devices) if (!entry.second.device.expired()) live++;
	return live;
}

std::vector<std::string> VkApriltagBackend::ListDevices()
{
	// vkapriltag's public API has no device list, so this asks Vulkan directly, in vkEnumeratePhysicalDevices order (the order its device_index uses)
	std::vector<std::string> names;
	VkInstance instance = VK_NULL_HANDLE;
	try {
		VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
		app.apiVersion = VK_API_VERSION_1_1;
		VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		info.pApplicationInfo = &app;
		VkResult result = vkCreateInstance(&info, nullptr, &instance);
		if (result != VK_SUCCESS) throw std::runtime_error("vkCreateInstance failed (VkResult " + std::to_string(static_cast<int>(result)) + ")");
		uint32_t count = 0;
		vkEnumeratePhysicalDevices(instance, &count, nullptr);
		std::vector<VkPhysicalDevice> devices(count);
		if (count > 0) vkEnumeratePhysicalDevices(instance, &count, devices.data());
		for (uint32_t i = 0; i < count; i++) {
			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties(devices[i], &properties);
			names.push_back(properties.deviceName);
		}
	} catch (const std::exception& e) {
		// no Vulkan loader or driver: no devices to choose from. Say why on stderr (the journal), otherwise an empty list looks like a machine without a GPU.
		std::cerr << "Vulkan device enumeration found no usable GPU: " << e.what() << std::endl;
	}
	if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
	return names;
}

VkApriltagBackend::VkApriltagBackend(int frameWidth, int frameHeight, ApriltagTuning tuning)
	: m_FrameWidth(frameWidth), m_FrameHeight(frameHeight), m_RefineMode(tuning.refineMode), m_RefineEdges(tuning.refineEdges)
{
	if (frameWidth <= 0 || frameHeight <= 0) {
		// the GPU pipeline's buffers are sized from these - there is no valid default
		throw std::invalid_argument("VkApriltagBackend needs the real frame size (got " +
			std::to_string(frameWidth) + "x" + std::to_string(frameHeight) + ")");
	}

	m_GpuWidth = (static_cast<int64_t>(frameWidth) * frameHeight) % 4 == 0 ? frameWidth : ((frameWidth + 3) & ~3);
	m_FamilyKind = tuning.family;
	m_MaxHamming = ClampMaxHamming(tuning.maxHamming);
	m_Family = CreateApriltagFamily(m_FamilyKind);
	m_ResultList = zarray_create(sizeof(apriltag_detection_t*));

	try {
		m_Decimation = ResolveDecimation(tuning.quadDecimate, m_GpuWidth, frameHeight);
		// no tag fits in a picture this small once decimated (and the pipeline's own buffers underflow below 3 pixels)
		if (std::min(frameWidth, frameHeight) / static_cast<int>(m_Decimation) < kMinDecimatedSide) {
			throw UnsupportedFrameSize("a " + std::to_string(frameWidth) + "x" + std::to_string(frameHeight) + " frame is too small for the GPU pipeline");
		}

		// from here a failure is about this frame size or this configuration, not about the device: the device was acquired
		SharedDevice shared = AcquireDevice(tuning.gpuDevice);
		try {
		m_Device = shared.device;
		m_GpuDeviceName = DeviceNameFrom(m_Device->Describe());
		// whether the driver accepts the pooled frames is learned on the first import: the library throws when it cannot
		m_HostImportAvailable = !FrameImportDisabledByEnv();

		DetectorOptions options;
		options.width = static_cast<uint32_t>(m_GpuWidth);
		options.height = static_cast<uint32_t>(frameHeight);
		options.decimation = m_Decimation;
		options.families = { FamilyName(m_FamilyKind) };
		options.refine_edges = tuning.refineEdges;
		// 4 threads by default rather than the whole CPU (GPU-bound with a small CPU tail); the worker pool is fixed at construction, so changing
		// this rebuilds the backend. Overridable per sink via ApriltagTuning.nthreads.
		m_Threads = tuning.nthreads > 0 ? static_cast<uint32_t>(tuning.nthreads) : 4;
		options.cpu_threads = m_Threads;
		options.stream = shared.stream;

		// The blob filters of the quad search: the sink's tuning when set, else the LUMEN_VK_MIN_* environment variables (for experiments), else the
		// library's own values. (APRILTAG_VK_MIN_DIFF / _MIN_TAG_PX / _PROBE_MARGIN still override all of these inside the library.)
		auto blobFilter = [](int tuned, const char* variable, uint32_t fallback) {
			if (tuned > 0) return static_cast<uint32_t>(tuned);
			const char* text = std::getenv(variable);
			if (!text || !*text) return fallback;
			char* end = nullptr;
			const unsigned long value = std::strtoul(text, &end, 10);
			return end && *end == '\0' ? static_cast<uint32_t>(value) : fallback;
		};
		options.min_white_black_diff = std::min<uint32_t>(255, blobFilter(tuning.minWhiteBlackDiff, "LUMEN_VK_MIN_WHITE_BLACK_DIFF", options.min_white_black_diff));
		options.min_cluster_pixels = blobFilter(tuning.minClusterPixels, "LUMEN_VK_MIN_CLUSTER_PIXELS", options.min_cluster_pixels);
		options.min_tag_pixels = blobFilter(tuning.minTagPixels, "LUMEN_VK_MIN_TAG_PIXELS", options.min_tag_pixels);
		options.probe_margin = tuning.probeMargin > 0.0f ? tuning.probeMargin : 0.0f;
		// the margin gate in effect: the library lets the environment variable win
		m_ProbeMargin = EnvFloat("APRILTAG_VK_PROBE_MARGIN", options.probe_margin);

		// the worker pool starts its threads here, so they take this thread's affinity: pin them to the performance cores
		// whichever thread (the sink's, or an API call's) is building the backend
		CpuAffinity::ScopedPerformanceAffinity performanceCores;
		m_VkDetector = std::make_unique<Detector>(options, m_Device);
		} catch (const UnsupportedFrameSize&) {
			throw;
		} catch (const std::exception& e) {
			throw UnsupportedFrameSize(e.what());
		}
	} catch (...) {
		ReleaseResources();
		throw;
	}
}

VkApriltagBackend::~VkApriltagBackend()
{
	ReleaseResources();
}

void VkApriltagBackend::ClearResult()
{
	for (apriltag_detection_t* detection : m_Result) {
		matd_destroy(detection->H);
		delete detection;
	}
	m_Result.clear();
	if (m_ResultList) zarray_clear(m_ResultList);
}

void VkApriltagBackend::ReleaseResources()
{
	// the detector before the device it runs on
	m_VkDetector.reset();
	m_Device.reset();

	ClearResult();
	if (m_ResultList) zarray_destroy(m_ResultList);
	m_ResultList = nullptr;
	if (m_Family) DestroyApriltagFamily(m_FamilyKind, m_Family);
	m_Family = nullptr;
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
		m_VkDetector->ImportHostFrame(data, bytes);
	} catch (const std::exception&) {
		// the driver refused this memory (or has no external host memory): stop trying and keep uploading
		m_HostImportAvailable = false;
		return false;
	}
	m_ImportedFrames.insert(data);
	return true;
}

zarray_t* VkApriltagBackend::Detect(const cv::Mat& frame)
{
	m_LastStageUs.clear();
	auto lapStart = std::chrono::steady_clock::now();
	auto lap = [&](const char* stage) {
		auto now = std::chrono::steady_clock::now();
		m_LastStageUs.emplace_back(stage, std::chrono::duration<double, std::micro>(now - lapStart).count());
		lapStart = now;
	};

	// the GPU buffers were sized for this frame size: a different one would be read or written past them
	if (frame.cols != m_FrameWidth || frame.rows != m_FrameHeight) {
		throw std::invalid_argument("VkApriltagBackend::Detect: the frame is " + std::to_string(frame.cols) + "x" + std::to_string(frame.rows) +
			" but the backend was built for " + std::to_string(m_FrameWidth) + "x" + std::to_string(m_FrameHeight));
	}

	// The GPU reads width*height packed bytes, so a crop carrying its parent's row stride is repacked first.
	cv::Mat packed;
	if (!frame.isContinuous()) {
		std::shared_ptr<void> owner;
		packed = FramePool::Instance().Acquire(frame.rows, frame.cols, frame.type(), owner);
		frame.copyTo(packed);
		m_PackedFrameOwner = owner;
		lap("vk-repack");
	}
	const cv::Mat& grayFrame = frame.isContinuous() ? frame : packed;

	// the picture the GPU reads: the frame itself, or its copy widened to a multiple of four pixels when the pixel count is odd-shaped
	cv::Mat gpuFrame = grayFrame;
	if (m_GpuWidth != m_FrameWidth) {
		m_PaddedFrame = FramePool::Instance().Acquire(grayFrame.rows, m_GpuWidth, CV_8UC1, m_PaddedFrameOwner);
		cv::copyMakeBorder(grayFrame, m_PaddedFrame, 0, 0, 0, m_GpuWidth - grayFrame.cols, cv::BORDER_REPLICATE);
		gpuFrame = m_PaddedFrame;
		lap("vk-pad");
	}

	m_LastFrameImported = ImportFrameIfPossible(gpuFrame);
	lapStart = std::chrono::steady_clock::now(); // the (rare) first-time import is setup, not a per-frame cost
	// the detector decodes against the picture it is given: the padded copy only repeats the last column, so corners are unchanged
	const Result& result = m_VkDetector->Detect(gpuFrame.data);

	// the library's own split of that call (GPU wall time, quad fit, refine + decode)
	m_LastStageUs.emplace_back("vk-gpu", result.timing.gpu_ms * 1000.0);
	m_LastStageUs.emplace_back("vk-quads", result.timing.quad_ms * 1000.0);
	m_LastStageUs.emplace_back("vk-tags", result.timing.decode_ms * 1000.0);

	ClearResult();
	for (const Detection& found : result.detections) {
		// the library corrects up to two wrong bits; a stricter limit drops the detections that needed more
		if (found.hamming > m_MaxHamming) continue;
		auto* detection = new apriltag_detection_t();
		detection->family = m_Family;
		detection->id = found.id;
		detection->hamming = found.hamming;
		detection->decision_margin = found.decision_margin;
		detection->H = matd_create_data(3, 3, found.H);
		detection->c[0] = found.c[0];
		detection->c[1] = found.c[1];
		for (int corner = 0; corner < 4; corner++) {
			detection->p[corner][0] = found.p[corner][0];
			detection->p[corner][1] = found.p[corner][1];
		}
		m_Result.push_back(detection);
		zarray_add(m_ResultList, &detection);
	}
	return m_ResultList;
}

void VkApriltagBackend::ReleaseResult(zarray_t* /*detections*/)
{
	// intentionally empty - see header
}

#endif // LUMEN_WITH_VULKAN_APRILTAG
