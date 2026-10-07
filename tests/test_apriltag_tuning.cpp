#include <catch2/catch_test_macros.hpp>
#include "ApriltagDetector.h"
#include "CpuApriltagBackend.h"
#include <catch2/generators/catch_generators.hpp>
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

#ifdef LUMEN_WITH_VULKAN_APRILTAG
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
