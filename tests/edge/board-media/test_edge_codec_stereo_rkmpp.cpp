// Hunter I (board-media): edge cases of CodecStereoBackend with codec-stereo's rkmpp_hwenc backend (VEPU580 H.264 encode + motion vectors).
#ifdef LUMEN_WITH_CODEC_STEREO
#include <catch2/catch_test_macros.hpp>
#include "CodecStereoBackend.h"
#include "edge_media_util.h"

#include <opencv2/opencv.hpp>
#include <memory>
#include <sstream>
#include <vector>

using namespace edge_media;

namespace {

CodecStereoBackend::Config HwencConfig()
{
	CodecStereoBackend::Config cfg;
	cfg.kind = STEREO_BACKEND_CODEC_RKMPP_HWENC;
	cfg.searchRangeX = 48;
	cfg.searchRangeY = 16;
	cfg.minDisparity = 0.0f;
	cfg.maxDy = 4;
	return cfg;
}

std::unique_ptr<CodecStereoBackend> MakeHwenc(const CodecStereoBackend::Config& cfg = HwencConfig())
{
	try {
		return std::make_unique<CodecStereoBackend>(cfg);
	} catch (const std::exception&) {
		return nullptr;
	}
}

// a textured pair whose right image is the left shifted by `shift` pixels
void ShiftedPair(int w, int h, int shift, cv::Mat& left, cv::Mat& right, unsigned seed = 1)
{
	cv::Mat base(h, w + shift, CV_8UC1);
	cv::RNG rng(seed);
	rng.fill(base, cv::RNG::UNIFORM, 0, 255);
	left = base(cv::Rect(shift, 0, w, h)).clone();
	right = base(cv::Rect(0, 0, w, h)).clone();
}


} // namespace

// A rectified-size change (calibration swapped, crop changed) rebuilds the MPP encoder; bad sizes fail in the middle of a build; backends come and
// go with the stereo node. None of it may leave an MPP context's fds or dma-bufs behind.
TEST_CASE("I: rkmpp_hwenc stereo leaves no fds or dma-bufs behind across size changes, failed sizes and backend churn", "[edge][board-media][rkmpp][leak]")
{
	{
		auto warm = MakeHwenc();
		if (!warm) SKIP("rkmpp_hwenc backend unavailable here");
		cv::Mat l, r;
		ShiftedPair(320, 192, 16, l, r);
		std::vector<float> d;
		int c, rr;
		if (!warm->Compute(l, r, d, c, rr)) SKIP("rkmpp_hwenc cannot compute here");
	}
	const FdCounts before = CountFds();
	const int mapsBefore = CountDmabufMaps();
	const std::pair<int, int> sizes[] = { { 320, 192 }, { 640, 384 }, { 32, 16 }, { 330, 190 }, { 640, 384 } };
	for (int round = 0; round < 4; round++) {
		auto backend = MakeHwenc();
		REQUIRE(backend);
		for (auto [w, h] : sizes) {
			cv::Mat l, r;
			ShiftedPair(w, h, 16, l, r);
			std::vector<float> d;
			int c, rr;
			backend->Compute(l, r, d, c, rr);
		}
	}
	const FdCounts after = CountFds();
	const int mapsAfter = CountDmabufMaps();
	INFO("fds " << before.total << " -> " << after.total << ", dmabuf fds " << before.dmabuf << " -> " << after.dmabuf << ", dmabuf mappings "
		<< mapsBefore << " -> " << mapsAfter);
	CHECK(after.total == before.total);
	CHECK(after.dmabuf == before.dmabuf);
	CHECK(mapsAfter == mapsBefore);
}

#endif // LUMEN_WITH_CODEC_STEREO
