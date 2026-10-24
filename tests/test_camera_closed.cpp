#include <catch2/catch_test_macros.hpp>
#include "CameraSource.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

// A camera that cannot be opened (unplugged, or moved to another USB port) must not make its capture thread spin and flood the log.
TEST_CASE("a camera that cannot be opened is retried slowly and reported once", "[camera][lifecycle]") {
	const std::filesystem::path logPath = std::filesystem::temp_directory_path() / "lumencore-test-camera-closed.log";
	std::error_code ignored;
	std::filesystem::remove(logPath, ignored);
	{
		auto logger = std::make_shared<Logger>(logPath.string());
		// a device that does not exist: a path nothing can open on Linux, an index no camera has on Windows
#ifdef _WIN32
		const std::string missing = "98";
#else
		const std::string missing = "/dev/does-not-exist";
#endif
		CameraFrameSource source(missing, "missing camera", logger, "missing-camera");
		source.Toggle(true);
		std::this_thread::sleep_for(std::chrono::milliseconds(1500));
		const auto stopStart = std::chrono::steady_clock::now();
		source.Toggle(false); // must not wait out the retry delay
		REQUIRE(std::chrono::steady_clock::now() - stopStart < std::chrono::milliseconds(500));
	}

	std::ifstream file(logPath);
	const std::string log((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	size_t reports = 0;
	for (size_t at = log.find("camera is closed"); at != std::string::npos; at = log.find("camera is closed", at + 1)) reports++;
	REQUIRE(reports == 1);

	file.close();
	std::filesystem::remove(logPath, ignored);
}
