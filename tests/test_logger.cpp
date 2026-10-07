#include <catch2/catch_test_macros.hpp>
#include "Logger.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <random>

namespace fs = std::filesystem;

namespace {

struct TempDir {
	fs::path path = fs::temp_directory_path() / ("lumen-logger-" + std::to_string(std::random_device{}()));
	TempDir() { fs::create_directories(path); }
	~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
	std::string File(const std::string& name) const { return (path / name).string(); }
};

// restores the shared rotation settings the other tests rely on
struct RotationGuard {
	int64_t bytes = Logger::GetMaxFileBytes();
	int kept = Logger::GetFilesKept();
	~RotationGuard() { Logger::SetRotation(bytes, kept); }
};

std::string Slurp(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}

void WriteLines(Logger& logger, int from, int to) {
	for (int i = from; i < to; i++) logger.EnterLog("line " + std::to_string(i) + " " + std::string(180, 'x'));
}

}

TEST_CASE("a log file rotates at the size limit and keeps only the configured number of old files", "[logger]") {
	RotationGuard guard;
	TempDir dir;
	Logger::SetRotation(64 * 1024, 2);
	{
		Logger logger(dir.File("core.log"));
		WriteLines(logger, 0, 1500); // about 300 KB: several rotations
	}

	REQUIRE(fs::exists(dir.File("core.log")));
	REQUIRE(fs::exists(dir.File("core.log.1")));
	REQUIRE(fs::exists(dir.File("core.log.2")));
	REQUIRE_FALSE(fs::exists(dir.File("core.log.3")));
	// no file is much bigger than the limit (a file rotates right after the write that crosses it)
	for (const char* name : { "core.log", "core.log.1", "core.log.2" })
		REQUIRE(fs::file_size(dir.File(name)) < 64 * 1024 + 1024);
	// the newest line is in the live file and older lines are in older files, in order
	REQUIRE(Slurp(dir.File("core.log")).find("line 1499 ") != std::string::npos);
	REQUIRE(Slurp(dir.File("core.log.1")).find("line 1499 ") == std::string::npos);
}

TEST_CASE("with no rotated files kept the live file simply restarts", "[logger]") {
	RotationGuard guard;
	TempDir dir;
	Logger::SetRotation(64 * 1024, 0);
	{
		Logger logger(dir.File("core.log"));
		WriteLines(logger, 0, 800);
	}

	REQUIRE(fs::exists(dir.File("core.log")));
	REQUIRE_FALSE(fs::exists(dir.File("core.log.1")));
	REQUIRE(fs::file_size(dir.File("core.log")) < 64 * 1024 + 1024);
}

TEST_CASE("a log file that grew past the limit before rotation existed is cut to its newest lines", "[logger]") {
	RotationGuard guard;
	TempDir dir;
	Logger::SetRotation(64 * 1024, 3);
	{
		std::ofstream old(dir.File("core.log"));
		for (int i = 0; i < 5000; i++) old << "[ERROR]: camera grab failed " << i << "\n";
	}
	REQUIRE(fs::file_size(dir.File("core.log")) > 128 * 1024);

	{
		Logger logger(dir.File("core.log"));
		logger.EnterLog("after the update");
	}

	REQUIRE(fs::file_size(dir.File("core.log")) < 64 * 1024);
	std::string text = Slurp(dir.File("core.log"));
	REQUIRE(text.find("camera grab failed 4999") != std::string::npos); // the newest old line survives
	REQUIRE(text.find("camera grab failed 0\r") == std::string::npos);
	REQUIRE(text.find("camera grab failed 0\n") == std::string::npos);
	REQUIRE(text.rfind("[ERROR]", 0) == 0);                              // starting on a whole line, not mid-line
	REQUIRE(text.find("after the update") != std::string::npos);
}

TEST_CASE("every log call still reaches the file, and nothing is written to stdout", "[logger]") {
	RotationGuard guard;
	TempDir dir;
	std::ostringstream captured;
	std::streambuf* original = std::cout.rdbuf(captured.rdbuf());
	{
		Logger logger(dir.File("core.log"));
		for (int i = 0; i < 50; i++) {
			logger.EnterLog("same message");
			logger.EnterLog(LogLevel::Error, "same error");
		}
	}
	std::cout.rdbuf(original);

	std::string text = Slurp(dir.File("core.log"));
	size_t infos = 0, errors = 0;
	for (size_t at = text.find("[INFO]: same message"); at != std::string::npos; at = text.find("[INFO]: same message", at + 1)) infos++;
	for (size_t at = text.find("[ERROR]: same error"); at != std::string::npos; at = text.find("[ERROR]: same error", at + 1)) errors++;
	REQUIRE(infos == 50);
	REQUIRE(errors == 50);
	REQUIRE(captured.str().empty());
}

TEST_CASE("rotation limits are clamped to sensible values", "[logger]") {
	RotationGuard guard;
	Logger::SetRotation(10, 500);
	REQUIRE(Logger::GetMaxFileBytes() >= 64 * 1024);
	REQUIRE(Logger::GetFilesKept() == 20);
	Logger::SetRotation(1024 * 1024, -4);
	REQUIRE(Logger::GetFilesKept() == 0);
}
