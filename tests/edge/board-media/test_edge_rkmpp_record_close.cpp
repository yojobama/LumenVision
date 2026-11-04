// Closing an h264_rkmpp recording used to make libmpp log "mpp_mem_pool_put invalid mem pool ptr ... caller mpp_frame_deinit": FFmpeg's rkmpp encoder
// freed its end-of-stream frame twice (see scripts/patches/ffmpeg-rockchip-rkmppenc-eos.patch). This runs a recording and closes it with stderr
// redirected to a file, so libmpp's message can be looked for. It needs the h264_rkmpp encoder, so it skips elsewhere; and it needs the patched
// ffmpeg-rockchip that scripts/install-deps.sh builds.
#ifdef __linux__
#include <catch2/catch_test_macros.hpp>
#include "ISource.h"
#include "RecordSink.h"
#include <opencv2/opencv.hpp>

#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#ifdef LUMEN_WITH_RECORD
namespace {
class EdgeFrameSource : public ISource {
public:
	EdgeFrameSource() : ISource(nullptr, "edge-rkmpp-close-source") { m_DoNotLoadCaptureThread = true; }
	void Publish(const cv::Mat& picture) { SetLatestResult(SourceResult(std::nullopt, Frame(picture, FrameFormat::BGR24))); }
};

// what was written to stderr while the scope lived
class StderrCapture {
public:
	explicit StderrCapture(const std::filesystem::path& file) : m_File(file)
	{
		std::fflush(stderr);
		m_Saved = dup(STDERR_FILENO);
		FILE* capture = std::fopen(file.string().c_str(), "w");
		dup2(fileno(capture), STDERR_FILENO);
		std::fclose(capture);
	}
	std::string Finish()
	{
		std::fflush(stderr);
		if (m_Saved >= 0) { dup2(m_Saved, STDERR_FILENO); close(m_Saved); m_Saved = -1; }
		std::ifstream in(m_File);
		std::stringstream text;
		text << in.rdbuf();
		return text.str();
	}
	~StderrCapture() { Finish(); }

private:
	std::filesystem::path m_File;
	int m_Saved = -1;
};
}

TEST_CASE("I: closing an h264_rkmpp recording does not make libmpp complain", "[edge][board-media][record]")
{
	const auto folder = std::filesystem::temp_directory_path() / "edge-rkmpp-close";
	std::filesystem::remove_all(folder);
	auto source = std::make_shared<EdgeFrameSource>();
	cv::Mat picture(480, 640, CV_8UC3);
	cv::randu(picture, 0, 255);

	std::string logged;
	bool encoderPresent = false;
	{
		StderrCapture capture(std::filesystem::temp_directory_path() / "edge-rkmpp-close.stderr");
		for (int round = 0; round < 3; round++) {
			RecordSinkConfig config;
			config.dstFolder = folder.string();
			config.encoderName = "h264_rkmpp";
			auto sink = std::make_shared<RecordSink>(nullptr, "edge-rkmpp-close", config);
			REQUIRE(sink->BindSource(source));
			sink->Toggle(true);
			for (int i = 0; i < 45; i++) {
				source->Publish(picture);
				std::this_thread::sleep_for(std::chrono::milliseconds(33));
			}
			sink->Toggle(false);
			encoderPresent = !sink->ListSegments().empty() || encoderPresent;
		}
		logged = capture.Finish();
	}
	if (!encoderPresent) SKIP("no h264_rkmpp encoder here (no segment was written)");

	INFO("libmpp said:\n" << logged);
	CHECK(logged.find("invalid mem pool ptr") == std::string::npos);
	CHECK(logged.find("used buffer") == std::string::npos);
	std::filesystem::remove_all(folder);
}
#endif // LUMEN_WITH_RECORD
#endif // __linux__
