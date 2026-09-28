#include <catch2/catch_test_macros.hpp>
#include "Manager.h"
#include <filesystem>
#include <opencv2/imgcodecs.hpp>

// Manager::SaveSnapshot writes a source latest frame to disk. ImageFileFrameSource publishes
// synchronously in its constructor, so no wait is needed.
TEST_CASE("Manager::SaveSnapshot writes a real, readable image file", "[snapshot]") {
	Manager manager;
	std::string busJpgPath = std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg";
	int sourceId = manager.CreateImageFileSource(busJpgPath);

	std::filesystem::path outPath = std::filesystem::temp_directory_path() / "lumencore-test-snapshot.png";
	std::filesystem::remove(outPath);

	REQUIRE(manager.SaveSnapshot(sourceId, outPath.string()));
	REQUIRE(std::filesystem::exists(outPath));

	cv::Mat written = cv::imread(outPath.string());
	REQUIRE_FALSE(written.empty());
	// bus.jpg is 640x640.
	REQUIRE(written.cols == 640);
	REQUIRE(written.rows == 640);

	std::filesystem::remove(outPath);
}

TEST_CASE("Manager::SaveSnapshot returns false for a source that hasn't published a frame", "[snapshot]") {
	Manager manager;
	// An ApriltagDetector bound to nothing never calls SetLatestResult, so the frame stays unset.
	int sinkId = manager.CreateApriltagDetector();

	std::filesystem::path outPath = std::filesystem::temp_directory_path() / "lumencore-test-snapshot-empty.png";
	REQUIRE_FALSE(manager.SaveSnapshot(sinkId, outPath.string()));
	REQUIRE_FALSE(std::filesystem::exists(outPath));
}
