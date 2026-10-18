#include <catch2/catch_test_macros.hpp>
#include "ApriltagDetector.h"
#include "CpuApriltagBackend.h"
#include "FramePool.h"
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_approx.hpp>
#include <cstdlib>
#ifdef LUMEN_WITH_VULKAN_APRILTAG
#include "VkApriltagBackend.h"
#endif

// The refine-edges mode is only honoured by the Vulkan backend; none of these tests needs a GPU.

TEST_CASE("the CPU backend ignores the refine mode and reports it unsupported", "[apriltag][tuning]") {
	ApriltagTuning tuning;
	tuning.nthreads = 1;
	tuning.refineMode = REFINE_ULTRAFAST;
	CpuApriltagBackend backend(tuning);

	REQUIRE_FALSE(backend.GetRefineModeSupported());
	REQUIRE(backend.GetRefineMode() == REFINE_UPSTREAM);
	REQUIRE(backend.GetRefineEdges());
}

TEST_CASE("a default tuning keeps the bit-identical exact refine mode", "[apriltag][tuning]") {
	REQUIRE(ApriltagTuning().refineMode == REFINE_EXACT);
}

TEST_CASE("ApriltagDetector passes the refine mode support flag through from its backend", "[apriltag][tuning]") {
	ApriltagTuning tuning;
	tuning.refineMode = REFINE_FAST;
	ApriltagDetector detector(nullptr, "tuning-test", CameraCalibrationResult(), 0.1651, APRILTAG_BACKEND_CPU, 0, 0, tuning);

	REQUIRE_FALSE(detector.GetRefineModeSupported());
	REQUIRE(detector.GetRefineMode() == REFINE_UPSTREAM);
	REQUIRE(detector.GetRequestedTuning().refineMode == REFINE_FAST);
}

#ifdef LUMEN_WITH_VULKAN_APRILTAG
TEST_CASE("each refine mode maps to its vkapriltag method", "[apriltag][tuning]") {
	using apriltag_vulkan::RefineEdgesMethod;
	REQUIRE(VkApriltagBackend::ToRefineMethod(REFINE_UPSTREAM) == RefineEdgesMethod::kUpstream);
	REQUIRE(VkApriltagBackend::ToRefineMethod(REFINE_EXACT) == RefineEdgesMethod::kExact);
	REQUIRE(VkApriltagBackend::ToRefineMethod(REFINE_FAST) == RefineEdgesMethod::kFast);
	REQUIRE(VkApriltagBackend::ToRefineMethod(REFINE_ULTRAFAST) == RefineEdgesMethod::kUltraFast);
}
#endif

// ---- family, blur, hamming and the detector-side filters ----

#include "ApriltagFamily.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {

// a tag of this family, scaled up and padded with white so the quad finder has a clean edge to follow
cv::Mat RenderTag(ApriltagFamilyKind kind, int id)
{
	apriltag_family_t* family = CreateApriltagFamily(kind);
	image_u8_t* image = apriltag_to_image(family, static_cast<uint32_t>(id));
	cv::Mat cells(image->height, image->width, CV_8UC1, image->buf, image->stride);
	cv::Mat scaled;
	cv::resize(cells, scaled, cv::Size(), 40, 40, cv::INTER_NEAREST);
	cv::Mat padded;
	cv::copyMakeBorder(scaled, padded, 60, 60, 60, 60, cv::BORDER_CONSTANT, cv::Scalar(255));
	image_u8_destroy(image);
	DestroyApriltagFamily(kind, family);
	return padded;
}

std::vector<int> DetectedIds(CpuApriltagBackend& backend, const cv::Mat& gray)
{
	std::vector<int> ids;
	zarray_t* detections = backend.Detect(gray);
	for (int i = 0; i < zarray_size(detections); i++) {
		apriltag_detection_t* detection;
		zarray_get(detections, i, &detection);
		ids.push_back(detection->id);
	}
	backend.ReleaseResult(detections);
	return ids;
}

class StillFrameSource : public ISource {
public:
	StillFrameSource(std::shared_ptr<Logger> logger, std::string id, cv::Mat gray)
		: ISource(logger, id), m_Bgr()
	{
		cv::cvtColor(gray, m_Bgr, cv::COLOR_GRAY2BGR);
	}

protected:
	void CaptureFrame() override {
		SetLatestResult(SourceResult(std::nullopt, m_Bgr));
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

private:
	cv::Mat m_Bgr;
};

// runs a detector on one still image for a moment and returns its envelope
nlohmann::json DetectOnce(const cv::Mat& gray, ApriltagTuning tuning)
{
	auto logger = std::make_shared<Logger>("LumenCoreTests-apriltag-tuning.log");
	auto source = std::make_shared<StillFrameSource>(logger, "tuning-source", gray);
	const double focal = 700.0;
	CameraCalibrationResult calibration(focal, focal, gray.cols / 2.0, gray.rows / 2.0, 0.2, {}, gray.cols, gray.rows);
	ApriltagDetector detector(logger, "tuning-detector", calibration, 0.1651, APRILTAG_BACKEND_CPU, 0, 0, tuning);

	REQUIRE(detector.BindSource(source));
	source->Toggle(true);
	static_cast<ISink&>(detector).Toggle(true);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	source->Toggle(false);
	static_cast<ISink&>(detector).Toggle(false);

	SourceResult result = detector.GetLatestResult();
	REQUIRE(result.json.has_value());
	return *result.json;
}

}

TEST_CASE("each tag family is detected only by a backend set to that family", "[apriltag][tuning]") {
	const ApriltagFamilyKind kind = GENERATE(APRILTAG_FAMILY_36H11, APRILTAG_FAMILY_16H5, APRILTAG_FAMILY_25H9, APRILTAG_FAMILY_STANDARD41H12);
	INFO("family " << static_cast<int>(kind));
	const cv::Mat image = RenderTag(kind, 3);

	ApriltagTuning matching;
	matching.family = kind;
	CpuApriltagBackend sameFamily(matching);
	REQUIRE(sameFamily.GetFamily() == kind);
	REQUIRE(DetectedIds(sameFamily, image) == std::vector<int>{ 3 });

	if (kind != APRILTAG_FAMILY_36H11) {
		CpuApriltagBackend defaultFamily; // tag36h11
		REQUIRE(DetectedIds(defaultFamily, image).empty());
	}
}

TEST_CASE("the hamming limit is clamped and the blur is reported", "[apriltag][tuning]") {
	ApriltagTuning tuning;
	tuning.maxHamming = 7;
	tuning.quadSigma = 0.8f;
	CpuApriltagBackend backend(tuning);

	REQUIRE(backend.GetMaxHamming() == 2);
	REQUIRE(backend.GetQuadSigma() == 0.8f);
	REQUIRE(backend.GetQuadSigmaSupported());

	tuning.maxHamming = -3;
	REQUIRE(CpuApriltagBackend(tuning).GetMaxHamming() == 0);
	REQUIRE(ApriltagTuning().maxHamming == 2); // the old behaviour: add_family's default of two corrected bits
}

TEST_CASE("the detector drops tags below the decision margin cutoff", "[apriltag][tuning]") {
	const cv::Mat image = RenderTag(APRILTAG_FAMILY_36H11, 5);

	ApriltagTuning lenient;
	nlohmann::json kept = DetectOnce(image, lenient);
	REQUIRE(kept["tags"].size() == 1);
	REQUIRE(kept["tags"][0]["id"] == 5);

	ApriltagTuning strict;
	strict.decisionMargin = 1.0e6f;
	REQUIRE(DetectOnce(image, strict)["tags"].empty());
}

TEST_CASE("turning single-tag poses off keeps the tag but drops its pose", "[apriltag][tuning]") {
	const cv::Mat image = RenderTag(APRILTAG_FAMILY_36H11, 5);

	ApriltagTuning withPose;
	nlohmann::json posed = DetectOnce(image, withPose);
	REQUIRE(posed["tags"][0].contains("pose"));

	ApriltagTuning noPose;
	noPose.singleTagPose = false;
	nlohmann::json flat = DetectOnce(image, noPose);
	REQUIRE(flat["tags"].size() == 1);
	REQUIRE_FALSE(flat["tags"][0].contains("pose"));
}

TEST_CASE("the detector reports the tuning in effect, including the knobs it applies itself", "[apriltag][tuning]") {
	ApriltagTuning tuning;
	tuning.family = APRILTAG_FAMILY_16H5;
	tuning.quadSigma = 0.5f;
	tuning.decisionMargin = 12.5f;
	tuning.poseIterations = 20;
	tuning.multiTag = false;
	ApriltagDetector detector(nullptr, "tuning-effective", CameraCalibrationResult(), 0.1651, APRILTAG_BACKEND_CPU, 0, 0, tuning);

	ApriltagTuning effective = detector.GetEffectiveTuning();
	REQUIRE(effective.family == APRILTAG_FAMILY_16H5);
	REQUIRE(effective.quadSigma == 0.5f);
	REQUIRE(effective.decisionMargin == 12.5f);
	REQUIRE(effective.poseIterations == 20);
	REQUIRE_FALSE(effective.multiTag);
	REQUIRE(detector.GetQuadSigmaSupported());
}

TEST_CASE("pooled frames are page-aligned, sized in whole pages, and recycled at the same address", "[framepool]") {
	std::shared_ptr<void> owner;
	cv::Mat first = FramePool::Instance().Acquire(481, 641, CV_8UC1, owner);
	const uint8_t* address = first.data;
	REQUIRE(reinterpret_cast<uintptr_t>(address) % FramePool::kBlockAlignment == 0);
	const size_t capacity = FramePool::Instance().BlockCapacity(address);
	REQUIRE(capacity >= 481u * 641u);
	REQUIRE(capacity % FramePool::kBlockAlignment == 0);
	REQUIRE(first.isContinuous());

	std::vector<uint8_t> foreign(16);
	REQUIRE(FramePool::Instance().BlockCapacity(foreign.data()) == 0);
	REQUIRE(FramePool::Instance().BlockCapacity(address + 1) == 0);

	first.release();
	owner.reset();
	std::shared_ptr<void> again;
	cv::Mat second = FramePool::Instance().Acquire(481, 641, CV_8UC1, again);
	REQUIRE(second.data == address);
}

TEST_CASE("a running detector given its camera's calibration afterwards starts posing, and a field layout then yields a multi-tag pose", "[apriltag][calibration]") {
	// two tags side by side: a multi-tag pose needs at least two layout tags in view
	cv::Mat image;
	cv::hconcat(RenderTag(APRILTAG_FAMILY_36H11, 3), RenderTag(APRILTAG_FAMILY_36H11, 4), image);
	auto logger = std::make_shared<Logger>("LumenCoreTests-apriltag-calibration.log");
	auto source = std::make_shared<StillFrameSource>(logger, "calibration-source", image);

	// a sink made from the graph starts like this: no calibration at all
	ApriltagDetector detector(logger, "calibration-detector", CameraCalibrationResult(), 0.1651, APRILTAG_BACKEND_CPU);
	REQUIRE_FALSE(detector.HasCalibration());

	const std::filesystem::path layoutPath = std::filesystem::temp_directory_path() / "lumen-calibration-test-layout.json";
	{
		std::ofstream layout(layoutPath);
		layout << R"({"tags":[{"ID":3,"pose":{"translation":{"x":4.0,"y":2.0,"z":0.5},"rotation":{"quaternion":{"W":1.0,"X":0.0,"Y":0.0,"Z":0.0}}}},{"ID":4,"pose":{"translation":{"x":4.0,"y":2.5,"z":0.5},"rotation":{"quaternion":{"W":1.0,"X":0.0,"Y":0.0,"Z":0.0}}}}],"field":{"length":16.5,"width":8.2}})";
	}
	REQUIRE(detector.LoadFieldLayout(layoutPath.string()));
	REQUIRE(detector.GetFieldLayoutTagCount() == 2);

	REQUIRE(detector.BindSource(source));
	source->Toggle(true);
	static_cast<ISink&>(detector).Toggle(true);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	nlohmann::json before = *detector.GetLatestResult().json;
	REQUIRE(before["tags"].size() == 2);
	REQUIRE(before["calibration"].is_null());
	REQUIRE_FALSE(before["tags"][0].contains("pose")); // the layout is loaded and the tag is seen, but without intrinsics nothing can be posed
	REQUIRE(before["multiTag"].is_null());

	const double focal = 700.0;
	detector.SetCalibration(CameraCalibrationResult(focal, focal, image.cols / 2.0, image.rows / 2.0, 0.2, {}, image.cols, image.rows));
	REQUIRE(detector.HasCalibration());
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	nlohmann::json after = *detector.GetLatestResult().json;
	REQUIRE_FALSE(after["calibration"].is_null());
	REQUIRE(after["calibration"]["fx"] == focal);
	REQUIRE(after["tags"][0].contains("pose"));
	REQUIRE_FALSE(after["multiTag"].is_null());
	REQUIRE(after["multiTag"]["fiducialIds"].size() == 2);

	// taking it away again returns to the uncalibrated behaviour
	detector.SetCalibration(CameraCalibrationResult());
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	REQUIRE(detector.GetLatestResult().json->at("calibration").is_null());

	source->Toggle(false);
	static_cast<ISink&>(detector).Toggle(false);
	std::filesystem::remove(layoutPath);
}

TEST_CASE("a tag is found in a crop of a larger gray frame, which is a view with the parent's row stride", "[apriltag][crop]") {
	// the left half is plain white, the tag is in the right half - as one eye of a side-by-side stereo camera sees it
	const cv::Mat tag = RenderTag(APRILTAG_FAMILY_36H11, 3);
	cv::Mat wide(tag.rows, tag.cols * 2, CV_8UC1, cv::Scalar(255));
	tag.copyTo(wide(cv::Rect(tag.cols, 0, tag.cols, tag.rows)));

	const cv::Mat rightEye = wide(cv::Rect(tag.cols, 0, tag.cols, tag.rows));
	REQUIRE_FALSE(rightEye.isContinuous()); // the situation under test: step is the parent's, not the crop's width
	const cv::Mat leftEye = wide(cv::Rect(0, 0, tag.cols, tag.rows));

	CpuApriltagBackend cpu;
	REQUIRE(DetectedIds(cpu, rightEye) == std::vector<int>{ 3 });
	REQUIRE(DetectedIds(cpu, leftEye).empty());

#ifdef LUMEN_WITH_VULKAN_APRILTAG
	ApriltagTuning tuning;
	tuning.quadDecimate = 1.0f;
	std::unique_ptr<VkApriltagBackend> vulkan;
	try {
		vulkan = std::make_unique<VkApriltagBackend>(tag.cols, tag.rows, tuning);
	} catch (const std::exception&) {
		return; // no Vulkan device here; the CPU half above still ran
	}
	zarray_t* found = vulkan->Detect(rightEye);
	REQUIRE(zarray_size(found) == 1);
	apriltag_detection_t* detection;
	zarray_get(found, 0, &detection);
	REQUIRE(detection->id == 3);
	const double x = detection->c[0], y = detection->c[1];
	// the same answer as for a packed copy of the same pixels
	zarray_t* packedFound = vulkan->Detect(rightEye.clone());
	REQUIRE(zarray_size(packedFound) == 1);
	apriltag_detection_t* packedDetection;
	zarray_get(packedFound, 0, &packedDetection);
	REQUIRE(packedDetection->c[0] == Catch::Approx(x).margin(1e-3));
	REQUIRE(packedDetection->c[1] == Catch::Approx(y).margin(1e-3));
	REQUIRE(zarray_size(vulkan->Detect(leftEye)) == 0);
#endif
}

TEST_CASE("the margin gate is off by default and the CPU backend, which has none, always reports it off", "[apriltag][tuning]") {
	REQUIRE(ApriltagTuning().probeMargin == 0.0f);

	ApriltagTuning tuning;
	tuning.probeMargin = 12.0f;
	ApriltagDetector detector(nullptr, "tuning-probe-margin", CameraCalibrationResult(), 0.1651, APRILTAG_BACKEND_CPU, 0, 0, tuning);
	REQUIRE(detector.GetRequestedTuning().probeMargin == 12.0f);
	REQUIRE(detector.GetProbeMargin() == 0.0f);
	REQUIRE(detector.GetEffectiveTuning().probeMargin == 0.0f);
}

#ifdef LUMEN_WITH_VULKAN_APRILTAG
TEST_CASE("the Vulkan backend applies the margin gate and still finds a clean tag", "[hitl][vulkan][apriltag][tuning]") {
	const cv::Mat image = RenderTag(APRILTAG_FAMILY_36H11, 3);
	ApriltagTuning tuning;
	tuning.quadDecimate = 1.0f;
	tuning.probeMargin = 10.0f;
	std::unique_ptr<VkApriltagBackend> backend;
	try {
		backend = std::make_unique<VkApriltagBackend>(image.cols, image.rows, tuning);
	} catch (const std::exception& e) {
		SKIP("no usable Vulkan compute device (" << e.what() << ")");
	}
	// APRILTAG_VK_PROBE_MARGIN, when set, wins over the request
	if (std::getenv("APRILTAG_VK_PROBE_MARGIN") == nullptr) REQUIRE(backend->GetProbeMargin() == 10.0f);

	zarray_t* detections = backend->Detect(image);
	REQUIRE(zarray_size(detections) == 1);
	apriltag_detection_t* detection;
	zarray_get(detections, 0, &detection);
	REQUIRE(detection->id == 3);
}

TEST_CASE("the Vulkan backend reads a pooled frame in place and finds the same tag as an uploaded copy", "[hitl][vulkan][apriltag][tuning]") {
	const cv::Mat image = RenderTag(APRILTAG_FAMILY_36H11, 3);
	ApriltagTuning tuning;
	tuning.quadDecimate = 1.0f;
	std::unique_ptr<VkApriltagBackend> backend;
	try {
		backend = std::make_unique<VkApriltagBackend>(image.cols, image.rows, tuning);
	} catch (const std::exception& e) {
		SKIP("no usable Vulkan compute device (" << e.what() << ")");
	}

	auto corners = [&](const cv::Mat& frame) {
		zarray_t* detections = backend->Detect(frame);
		REQUIRE(zarray_size(detections) == 1);
		apriltag_detection_t* detection;
		zarray_get(detections, 0, &detection);
		REQUIRE(detection->id == 3);
		std::vector<cv::Point2d> points;
		for (auto& p : detection->p) points.emplace_back(p[0], p[1]);
		return points;
	};

	std::shared_ptr<void> owner;
	cv::Mat pooled = FramePool::Instance().Acquire(image.rows, image.cols, CV_8UC1, owner);
	image.copyTo(pooled);

	const std::vector<cv::Point2d> uploaded = corners(image); // not a pool buffer: always uploaded
	REQUIRE(backend->GetIngestMode() == "copy");
	const std::vector<cv::Point2d> inPlace = corners(pooled);
	const bool importable = backend->GetIngestMode() == "host-import";
	WARN("host import " << (importable ? "active" : "not available on this device"));
	for (size_t i = 0; i < 4; i++) {
		REQUIRE(inPlace[i].x == Catch::Approx(uploaded[i].x).margin(1e-4));
		REQUIRE(inPlace[i].y == Catch::Approx(uploaded[i].y).margin(1e-4));
	}
	// a second frame in the same buffer is still read correctly after the first import
	image.copyTo(pooled);
	corners(pooled);
}

TEST_CASE("Vulkan sinks share one context per GPU, each with its own lane, and it is released with the last of them", "[hitl][vulkan][apriltag][tuning]") {
	const cv::Mat image = RenderTag(APRILTAG_FAMILY_36H11, 3);
	ApriltagTuning tuning;
	tuning.quadDecimate = 1.0f;
	REQUIRE(VkApriltagBackend::LiveContextCount() == 0);

	std::unique_ptr<VkApriltagBackend> first, second, third;
	try {
		first = std::make_unique<VkApriltagBackend>(image.cols, image.rows, tuning);
	} catch (const std::exception& e) {
		SKIP("no usable Vulkan compute device (" << e.what() << ")");
	}
	second = std::make_unique<VkApriltagBackend>(image.cols, image.rows, tuning);
	// a different frame size is a different pipeline but the same device context
	third = std::make_unique<VkApriltagBackend>(image.cols / 2 * 2, image.rows, tuning);
	REQUIRE(VkApriltagBackend::LiveContextCount() == 1);
	REQUIRE(first->GetGpuDeviceName() == second->GetGpuDeviceName());

	for (VkApriltagBackend* backend : { first.get(), second.get(), third.get() }) {
		for (int frame = 0; frame < 3; frame++) { // interleaved, as sinks on separate cameras are
			zarray_t* detections = backend->Detect(image);
			REQUIRE(zarray_size(detections) == 1);
			apriltag_detection_t* detection;
			zarray_get(detections, 0, &detection);
			REQUIRE(detection->id == 3);
		}
	}

	first.reset();
	REQUIRE(VkApriltagBackend::LiveContextCount() == 1); // the others still hold it
	REQUIRE(zarray_size(second->Detect(image)) == 1);
	second.reset();
	third.reset();
	REQUIRE(VkApriltagBackend::LiveContextCount() == 0);

	// and a later sink gets a fresh one
	VkApriltagBackend again(image.cols, image.rows, tuning);
	REQUIRE(VkApriltagBackend::LiveContextCount() == 1);
	REQUIRE(zarray_size(again.Detect(image)) == 1);
}

TEST_CASE("the Vulkan backend detects every tag family too, and reports no blur support", "[hitl][vulkan][apriltag][tuning]") {
	const ApriltagFamilyKind kind = GENERATE(APRILTAG_FAMILY_36H11, APRILTAG_FAMILY_16H5, APRILTAG_FAMILY_25H9, APRILTAG_FAMILY_STANDARD41H12);
	INFO("family " << static_cast<int>(kind));
	const cv::Mat image = RenderTag(kind, 3);

	ApriltagTuning tuning;
	tuning.family = kind;
	tuning.quadDecimate = 1.0f;
	std::unique_ptr<VkApriltagBackend> backend;
	try {
		backend = std::make_unique<VkApriltagBackend>(image.cols, image.rows, tuning);
	} catch (const std::exception& e) {
		SKIP("no usable Vulkan compute device (" << e.what() << ")");
	}
	REQUIRE(backend->GetFamily() == kind);
	REQUIRE_FALSE(backend->GetQuadSigmaSupported());

	zarray_t* detections = backend->Detect(image);
	REQUIRE(zarray_size(detections) == 1);
	apriltag_detection_t* detection;
	zarray_get(detections, 0, &detection);
	REQUIRE(detection->id == 3);
}
#endif
