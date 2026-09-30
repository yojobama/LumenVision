#include <catch2/catch_test_macros.hpp>
#include "ApriltagDetector.h"
#include "CpuApriltagBackend.h"
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
