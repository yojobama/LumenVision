// Hunter I (board-media): edge cases of MppJpegDecoder, the RK3588 JPEG-VPU decode behind V4l2CameraBackend's MJPEG cameras.
#ifdef LUMEN_WITH_MPP_JPEG
#include <catch2/catch_test_macros.hpp>
#include "MppJpegDecoder.h"
#include "edge_media_util.h"
#include "YuvRange.h"

#include <rockchip/mpp_buffer.h>
#include <opencv2/opencv.hpp>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
#include <memory>
#include <sstream>
#include <vector>

using namespace edge_media;

namespace {

cv::Mat BusImage()
{
	static cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg", cv::IMREAD_COLOR);
	return bus;
}

// bus.jpg resized to w x h (a natural, colourful picture)
cv::Mat Picture(int w, int h)
{
	cv::Mat out;
	cv::resize(BusImage(), out, cv::Size(w, h), 0, 0, cv::INTER_AREA);
	return out;
}

enum class Sampling { S420, S422, S444, S440, S411, Gray };

std::vector<uint8_t> EncodeJpeg(const cv::Mat& bgr, Sampling sampling, int quality = 90, bool progressive = false, int restartInterval = 0)
{
	std::vector<int> params{ cv::IMWRITE_JPEG_QUALITY, quality };
	if (progressive) { params.push_back(cv::IMWRITE_JPEG_PROGRESSIVE); params.push_back(1); }
	if (restartInterval > 0) { params.push_back(cv::IMWRITE_JPEG_RST_INTERVAL); params.push_back(restartInterval); }
	cv::Mat image = bgr;
	switch (sampling) {
	case Sampling::Gray: cv::cvtColor(bgr, image, cv::COLOR_BGR2GRAY); break;
	case Sampling::S420: params.push_back(cv::IMWRITE_JPEG_SAMPLING_FACTOR); params.push_back(cv::IMWRITE_JPEG_SAMPLING_FACTOR_420); break;
	case Sampling::S422: params.push_back(cv::IMWRITE_JPEG_SAMPLING_FACTOR); params.push_back(cv::IMWRITE_JPEG_SAMPLING_FACTOR_422); break;
	case Sampling::S444: params.push_back(cv::IMWRITE_JPEG_SAMPLING_FACTOR); params.push_back(cv::IMWRITE_JPEG_SAMPLING_FACTOR_444); break;
	case Sampling::S440: params.push_back(cv::IMWRITE_JPEG_SAMPLING_FACTOR); params.push_back(cv::IMWRITE_JPEG_SAMPLING_FACTOR_440); break;
	case Sampling::S411: params.push_back(cv::IMWRITE_JPEG_SAMPLING_FACTOR); params.push_back(cv::IMWRITE_JPEG_SAMPLING_FACTOR_411); break;
	}
	std::vector<uint8_t> jpeg;
	cv::imencode(".jpg", image, jpeg, params);
	return jpeg;
}

double MeanAbsDiff(const cv::Mat& a, const cv::Mat& b)
{
	if (a.size() != b.size() || a.type() != b.type()) return 1e9;
	cv::Mat diff;
	cv::absdiff(a, b, diff);
	const cv::Scalar m = cv::mean(diff);
	return (m[0] + m[1] + m[2]) / a.channels();
}

// whether the decoder's two plane views lie inside the MppBuffer that owns them
bool PlanesInsideBuffer(const MppJpegDecoder::Nv12Planes& planes, std::string& why)
{
	MppBuffer buffer = static_cast<MppBuffer>(planes.owner.get());
	if (!buffer) { why = "no owner"; return false; }
	const uint8_t* base = static_cast<const uint8_t*>(mpp_buffer_get_ptr(buffer));
	const size_t size = mpp_buffer_get_size(buffer);
	const uint8_t* yEnd = planes.y.data + planes.y.step[0] * (planes.y.rows - 1) + planes.y.cols;
	const uint8_t* uvEnd = planes.uv.data + planes.uv.step[0] * (planes.uv.rows - 1) + planes.uv.cols * 2;
	std::ostringstream s;
	s << "buffer " << size << " bytes; y ends at +" << (yEnd - base) << ", uv starts at +" << (planes.uv.data - base) << " ends at +" << (uvEnd - base)
	  << " (y stride " << planes.y.step[0] << ")";
	why = s.str();
	return planes.y.data >= base && yEnd <= base + size && planes.uv.data >= base && uvEnd <= base + size;
}


} // namespace

// ---- tests ----
// Frames whose JPEG size differs from the negotiated mode (a camera that ignores S_FMT for some frames), sizes off the 16-pixel grid, odd sizes,
// damaged and progressive frames: each is decoded into views that stay inside the decoder's buffer, or declined.
TEST_CASE("I: MppJpegDecoder planes stay inside their buffer whatever size the JPEG really is", "[edge][board-media][mpp]")
{
	struct Case { const char* name; std::vector<uint8_t> jpeg; int w, h; };
	const std::vector<uint8_t> good = EncodeJpeg(Picture(640, 480), Sampling::S420);
	std::vector<Case> cases;
	cases.push_back({ "650x490", EncodeJpeg(Picture(650, 490), Sampling::S420), 650, 490 });
	cases.push_back({ "641x481 declared 640x480", EncodeJpeg(Picture(641, 481), Sampling::S420), 640, 480 });
	cases.push_back({ "641x481 declared 641x481", EncodeJpeg(Picture(641, 481), Sampling::S420), 641, 481 });
	cases.push_back({ "1280x720 declared 640x480", EncodeJpeg(Picture(1280, 720), Sampling::S420), 640, 480 });
	cases.push_back({ "1920x1080 declared 64x64", EncodeJpeg(Picture(1920, 1080), Sampling::S420), 64, 64 });
	cases.push_back({ "320x240 declared 640x480", EncodeJpeg(Picture(320, 240), Sampling::S420), 640, 480 });
	cases.push_back({ "2x2", EncodeJpeg(Picture(2, 2), Sampling::S420), 2, 2 });
	cases.push_back({ "progressive", EncodeJpeg(Picture(640, 480), Sampling::S420, 90, true), 640, 480 });
	cases.push_back({ "truncated", std::vector<uint8_t>(good.begin(), good.begin() + good.size() / 2), 640, 480 });
	cases.push_back({ "SOI+EOI", { 0xFF, 0xD8, 0xFF, 0xD9 }, 640, 480 });
	for (const Case& c : cases) {
		MppJpegDecoder decoder;
		MppJpegDecoder::Nv12Planes planes;
		const bool ok = decoder.DecodeNv12(c.jpeg.data(), c.jpeg.size(), c.w, c.h, planes);
		std::string why;
		INFO(c.name << ": " << (ok ? "decoded" : "declined"));
		if (ok) {
			const bool inside = PlanesInsideBuffer(planes, why);
			INFO(why);
			CHECK(inside);
			CHECK(planes.y.cols == c.w);
			CHECK(planes.y.rows == c.h);
		} else {
			SUCCEED();
		}
	}
}


// A single-component (greyscale) JPEG - what a monochrome MJPEG camera may send - has no chroma. The decoder block reports it as NV12 anyway and
// leaves the chroma plane unwritten, so DecodeNv12 hands consumers a frame whose colour planes are whatever the buffer held: zeros in a new buffer
// (a strongly green picture once converted to BGR), the previous frame's colour in a reused one.
TEST_CASE("I: a greyscale MJPEG frame decodes to its grey picture, not a green one", "[edge][board-media][mpp]")
{
	const cv::Mat picture = Picture(640, 480);
	const std::vector<uint8_t> grey = EncodeJpeg(picture, Sampling::Gray);
	cv::Mat reference = cv::imdecode(grey, cv::IMREAD_COLOR);
	REQUIRE(reference.size() == cv::Size(640, 480));

	MppJpegDecoder decoder;
	cv::Mat bgr;
	if (!decoder.Decode(grey.data(), grey.size(), 640, 480, false, bgr)) {
		// declining the frame is fine: the camera's software decode handles it
		SUCCEED("hardware decode declined a greyscale JPEG");
		return;
	}
	const double diff = MeanAbsDiff(bgr, reference);
	INFO("hardware BGR vs libjpeg BGR mean abs diff " << diff << "; mean BGR " << cv::mean(bgr) << " vs " << cv::mean(reference));
	CHECK(diff < 5.0);

	// the NV12 the camera actually publishes: grey has neutral chroma (128) everywhere
	MppJpegDecoder::Nv12Planes planes;
	REQUIRE(decoder.DecodeNv12(grey.data(), grey.size(), 640, 480, planes));
	const cv::Scalar uvMean = cv::mean(planes.uv);
	INFO("chroma plane mean " << uvMean[0] << ", " << uvMean[1]);
	CHECK(std::abs(uvMean[0] - 128.0) < 3.0);
	CHECK(std::abs(uvMean[1] - 128.0) < 3.0);
}

// The same, after a colour frame of the same size: the reused output buffer still holds that frame's chroma, so the grey frame comes out in the
// previous picture's colours.
TEST_CASE("I: a greyscale MJPEG frame after a colour one does not carry the colour frame's chroma", "[edge][board-media][mpp]")
{
	const cv::Mat colour = Picture(640, 480);
	cv::Mat flat(480, 640, CV_8UC3, cv::Scalar(128, 128, 128)); // mid grey everywhere
	const std::vector<uint8_t> colourJpeg = EncodeJpeg(colour, Sampling::S420);
	const std::vector<uint8_t> greyJpeg = EncodeJpeg(flat, Sampling::Gray);

	MppJpegDecoder decoder;
	cv::Mat first, second;
	if (!decoder.Decode(colourJpeg.data(), colourJpeg.size(), 640, 480, false, first)) SKIP("no working JPEG VPU here");
	if (!decoder.Decode(greyJpeg.data(), greyJpeg.size(), 640, 480, false, second)) {
		SUCCEED("hardware decode declined a greyscale JPEG");
		return;
	}
	// a flat grey JPEG decodes to flat grey: every channel ~128, no colour variation
	cv::Scalar mean, stddev;
	cv::meanStdDev(second, mean, stddev);
	INFO("decoded grey frame: mean " << mean << " stddev " << stddev << "; vs the earlier colour frame diff " << MeanAbsDiff(second, first));
	CHECK(std::abs(mean[0] - 128.0) < 4.0);
	CHECK(std::abs(mean[1] - 128.0) < 4.0);
	CHECK(std::abs(mean[2] - 128.0) < 4.0);
	CHECK(stddev[0] < 4.0);
	CHECK(stddev[2] < 4.0);
}

// Modes changing back and forth (each change past 2x replaces the output buffer group), damaged frames in between, frames held past the next decode
// and past the decoder itself, and decoders created and destroyed: no MPP/dma-buf fd or mapping may be left behind.
TEST_CASE("I: MppJpegDecoder leaves no dma-buf or MPP fds behind across mode changes, bad frames and decoder churn", "[edge][board-media][mpp][leak]")
{
	const std::vector<uint8_t> small = EncodeJpeg(Picture(320, 240), Sampling::S420);
	const std::vector<uint8_t> mid = EncodeJpeg(Picture(640, 480), Sampling::S422);
	const std::vector<uint8_t> big = EncodeJpeg(Picture(1920, 1080), Sampling::S420);
	std::vector<uint8_t> bad = mid;
	bad.resize(bad.size() / 3);
	{
		// warm-up: MPP's process-wide services (buffer service, device fds) open once and stay
		MppJpegDecoder warm;
		cv::Mat dst;
		if (!warm.Decode(mid.data(), mid.size(), 640, 480, false, dst)) SKIP("no working JPEG VPU here");
	}
	const FdCounts before = CountFds();
	const int mapsBefore = CountDmabufMaps();
	{
		std::vector<MppJpegDecoder::Nv12Planes> held; // a few frames kept alive, as a slow consumer would
		for (int cycle = 0; cycle < 5; cycle++) {
			MppJpegDecoder decoder;
			for (int i = 0; i < 30; i++) {
				const std::vector<uint8_t>* jpeg = i % 5 == 0 ? &big : i % 5 == 1 ? &small : i % 5 == 4 ? &bad : &mid;
				const int w = jpeg == &big ? 1920 : jpeg == &small ? 320 : 640, h = jpeg == &big ? 1080 : jpeg == &small ? 240 : 480;
				MppJpegDecoder::Nv12Planes planes;
				if (decoder.DecodeNv12(jpeg->data(), jpeg->size(), w, h, planes) && i % 7 == 0) held.push_back(planes);
			}
		}
		// frames outlive their decoders here, then go
		CHECK(held.size() > 0);
	}
	const FdCounts after = CountFds();
	const int mapsAfter = CountDmabufMaps();
	INFO("fds " << before.total << " -> " << after.total << ", dmabuf fds " << before.dmabuf << " -> " << after.dmabuf << ", mpp fds " << before.mpp << " -> "
		<< after.mpp << ", dmabuf mappings " << mapsBefore << " -> " << mapsAfter);
	CHECK(after.total == before.total);
	CHECK(after.dmabuf == before.dmabuf);
	CHECK(mapsAfter == mapsBefore);
}

namespace {
// the JPEG with every DHT (Huffman table) segment removed - the "AVI1" MJPEG many UVC webcams send, relying on the standard tables
std::vector<uint8_t> StripHuffmanTables(const std::vector<uint8_t>& jpeg)
{
	std::vector<uint8_t> out{ jpeg[0], jpeg[1] };
	size_t i = 2;
	while (i + 4 <= jpeg.size() && jpeg[i] == 0xFF) {
		const uint8_t marker = jpeg[i + 1];
		const size_t length = (static_cast<size_t>(jpeg[i + 2]) << 8) | jpeg[i + 3];
		if (marker == 0xDA) break; // start of scan: the rest is entropy-coded data up to EOI
		if (marker != 0xC4) out.insert(out.end(), jpeg.begin() + i, jpeg.begin() + i + 2 + length);
		i += 2 + length;
	}
	out.insert(out.end(), jpeg.begin() + i, jpeg.end());
	return out;
}
}

// What camera drivers actually hand over: a JPEG without Huffman tables, one followed by padding (a driver reporting the whole buffer as used,
// which Grab() also falls back to when bytesused is 0), one with junk after EOI. Each must decode to the picture or be declined.
TEST_CASE("I: MJPEG frames as UVC cameras send them decode to the right picture or fall back", "[edge][board-media][mpp]")
{
	const cv::Mat picture = Picture(640, 480);
	const std::vector<uint8_t> jpeg = EncodeJpeg(picture, Sampling::S422);
	cv::Mat reference = cv::imdecode(jpeg, cv::IMREAD_COLOR);
	struct Variant { const char* name; std::vector<uint8_t> bytes; };
	std::vector<Variant> variants;
	variants.push_back({ "no DHT", StripHuffmanTables(jpeg) });
	std::vector<uint8_t> padded = jpeg;
	padded.resize(640 * 480 * 2, 0);
	variants.push_back({ "zero padded to the buffer size", padded });
	std::vector<uint8_t> junk = jpeg;
	for (int i = 0; i < 5000; i++) junk.push_back(static_cast<uint8_t>(i * 37));
	variants.push_back({ "junk after EOI", junk });
	REQUIRE(variants[0].bytes.size() < jpeg.size());

	for (const Variant& v : variants) {
		MppJpegDecoder decoder;
		cv::Mat bgr;
		const bool ok = decoder.Decode(v.bytes.data(), v.bytes.size(), 640, 480, false, bgr);
		INFO(v.name << ": hardware decode " << (ok ? "accepted" : "declined") << (ok ? ", diff vs libjpeg of the intact frame " + std::to_string(MeanAbsDiff(bgr, reference)) : std::string()));
		if (ok) CHECK(MeanAbsDiff(bgr, reference) < 5.0);
		else SUCCEED();
	}
}

// The capture thread decodes while the frames it handed out are converted and dropped on consumer threads (a sink's processing thread), and the
// mode changes underneath (the output buffer group is replaced while the old group's frames are still held elsewhere).
TEST_CASE("I: MppJpegDecoder frames released on other threads while decoding continues and the mode changes", "[edge][board-media][mpp][threads]")
{
	const std::vector<uint8_t> small = EncodeJpeg(Picture(320, 240), Sampling::S420);
	const std::vector<uint8_t> big = EncodeJpeg(Picture(1280, 720), Sampling::S420);
	MppJpegDecoder decoder;
	{
		MppJpegDecoder::Nv12Planes probe;
		if (!decoder.DecodeNv12(small.data(), small.size(), 320, 240, probe)) SKIP("no working JPEG VPU here");
	}
	std::mutex mutex;
	std::condition_variable cv;
	std::vector<MppJpegDecoder::Nv12Planes> queue;
	bool done = false;
	std::atomic<int> converted{ 0 };
	auto consumer = [&] {
		for (;;) {
			MppJpegDecoder::Nv12Planes planes;
			{
				std::unique_lock<std::mutex> lock(mutex);
				cv.wait(lock, [&] { return done || !queue.empty(); });
				if (queue.empty()) return;
				planes = std::move(queue.back());
				queue.pop_back();
			}
			cv::Mat bgr;
			YuvRange::FullRangeNv12ToBgr(planes.y, planes.uv, bgr);
			if (!bgr.empty()) converted++;
		}
	};
	std::thread a(consumer), b(consumer);
	int decoded = 0;
	for (int i = 0; i < 120; i++) {
		const bool useBig = (i / 10) % 2 == 1;
		const std::vector<uint8_t>& jpeg = useBig ? big : small;
		MppJpegDecoder::Nv12Planes planes;
		if (decoder.DecodeNv12(jpeg.data(), jpeg.size(), useBig ? 1280 : 320, useBig ? 720 : 240, planes)) {
			decoded++;
			std::lock_guard<std::mutex> lock(mutex);
			queue.push_back(std::move(planes));
			cv.notify_one();
		}
	}
	{
		std::lock_guard<std::mutex> lock(mutex);
		done = true;
	}
	cv.notify_all();
	a.join();
	b.join();
	CHECK(decoded == 120);
	CHECK(converted.load() == decoded);
}

#endif // LUMEN_WITH_MPP_JPEG
