// Hunter I (board-media): edge cases of MjpegHwEncoder (FFmpeg's mjpeg_rkmpp on the RK3588 JPEG encoder), the MJPEG previews' encoder.
#include <catch2/catch_test_macros.hpp>
#include "MjpegHwEncoder.h"
#include "edge_media_util.h"

#include <opencv2/opencv.hpp>
#include <sstream>
#include <vector>

using namespace edge_media;

namespace {

cv::Mat BusPicture(int w, int h)
{
	static cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg", cv::IMREAD_COLOR);
	cv::Mat out;
	cv::resize(bus, out, cv::Size(w, h), 0, 0, cv::INTER_AREA);
	return out;
}

// a BGR picture as a full-range (JPEG) NV12 Frame (even sides) - what the hardware JPEG decoder hands a camera's consumers
Frame Nv12FromBgr(const cv::Mat& bgr)
{
	cv::Mat ycrcb;
	cv::cvtColor(bgr, ycrcb, cv::COLOR_BGR2YCrCb);
	std::vector<cv::Mat> ch;
	cv::split(ycrcb, ch);
	cv::Mat cb, cr, uv;
	cv::resize(ch[2], cb, cv::Size(bgr.cols / 2, bgr.rows / 2), 0, 0, cv::INTER_AREA);
	cv::resize(ch[1], cr, cv::Size(bgr.cols / 2, bgr.rows / 2), 0, 0, cv::INTER_AREA);
	cv::merge(std::vector<cv::Mat>{ cb, cr }, uv);
	return Frame::FromNv12(ch[0], uv, nullptr);
}

// the same picture as a full-range NV12 Frame (even sides) - what the hardware JPEG decoder hands a camera's consumers
Frame Nv12Picture(int w, int h)
{
	return Nv12FromBgr(BusPicture(w, h));
}

double MeanAbsDiff(const cv::Mat& a, const cv::Mat& b)
{
	if (a.size() != b.size() || a.type() != b.type()) return 1e9;
	cv::Mat diff;
	cv::absdiff(a, b, diff);
	const cv::Scalar m = cv::mean(diff);
	return (m[0] + m[1] + m[2]) / a.channels();
}


} // namespace

// One MjpegSink encodes every source it is given with one encoder: tiles of different sizes and formats, and the quality slider moving, reopen the
// hardware encoder on almost every frame. Neither that nor encoders coming and going may leave fds or dma-bufs behind.
TEST_CASE("I: MjpegHwEncoder leaves no fds or dma-bufs behind when sizes, formats and quality keep changing", "[edge][board-media][mjpeg][leak]")
{
	const Frame nv12Big = Nv12Picture(1280, 720), nv12Small = Nv12Picture(320, 240);
	const Frame bgr(BusPicture(640, 480));
	std::vector<uint8_t> jpeg;
	{
		MjpegHwEncoder warm;
		if (!warm.Encode(nv12Big, 80, 1, jpeg)) SKIP("no hardware JPEG encoder here: " + warm.LastError());
	}
	const FdCounts before = CountFds();
	const int mapsBefore = CountDmabufMaps();
	const long rssBefore = VmRssKb();
	for (int round = 0; round < 4; round++) {
		MjpegHwEncoder encoder;
		for (int i = 0; i < 40; i++) {
			const Frame& frame = i % 3 == 0 ? nv12Big : i % 3 == 1 ? nv12Small : bgr;
			CHECK(encoder.Encode(frame, 50 + (i % 5) * 10, 1 + i % 2, jpeg));
		}
	}
	const FdCounts after = CountFds();
	const int mapsAfter = CountDmabufMaps();
	INFO("fds " << before.total << " -> " << after.total << ", dmabuf fds " << before.dmabuf << " -> " << after.dmabuf << ", dmabuf mappings "
		<< mapsBefore << " -> " << mapsAfter << ", RSS " << rssBefore << " -> " << VmRssKb() << " kB");
	CHECK(after.total == before.total);
	CHECK(after.dmabuf == before.dmabuf);
	CHECK(mapsAfter == mapsBefore);
}

namespace {
// a picture with a lot of fine detail: what a camera at high gain in a dim room, or a close-up of carpet or foliage, looks like
cv::Mat DetailedPicture(int w, int h, double noiseSigma)
{
	cv::Mat picture = BusPicture(w, h), noise(h, w, CV_16SC3), out;
	cv::randn(noise, 0, noiseSigma);
	picture.convertTo(out, CV_16SC3);
	out += noise;
	out.convertTo(out, CV_8UC3);
	return out;
}
}

// The MJPEG quality setting goes up to 100 (MjpegSink::SetSettings, the web UI's slider). At the top of that range a detailed frame comes back from
// Encode() as true with a JPEG that does not decode at all, so the stream shows a broken frame instead of falling back to the CPU encoder.
TEST_CASE("I: a detailed frame at the top quality settings encodes to a JPEG that decodes, or falls back", "[edge][board-media][mjpeg]")
{
	struct Case { const char* name; Frame frame; int quality; };
	cv::Mat noise(480, 640, CV_8UC3);
	cv::randu(noise, 0, 256);
	std::vector<Case> cases;
	cases.push_back({ "noise nv12 640x480 q100", Nv12FromBgr(noise), 100 });
	cases.push_back({ "noise nv12 640x480 q99", Nv12FromBgr(noise), 99 });
	cases.push_back({ "noise bgr 640x480 q100", Frame(noise), 100 });
	cases.push_back({ "detailed nv12 1280x720 sigma 20 q100", Nv12FromBgr(DetailedPicture(1280, 720, 20)), 100 });
	cases.push_back({ "detailed nv12 1280x720 sigma 20 q97", Nv12FromBgr(DetailedPicture(1280, 720, 20)), 97 });
	MjpegHwEncoder encoder;
	std::vector<uint8_t> jpeg;
	if (!encoder.Encode(cases[0].frame, 80, 1, jpeg)) SKIP("no hardware JPEG encoder here: " + encoder.LastError());
	for (const Case& c : cases) {
		const bool ok = encoder.Encode(c.frame, c.quality, 1, jpeg);
		cv::Mat decoded = ok ? cv::imdecode(jpeg, cv::IMREAD_COLOR) : cv::Mat();
		INFO(c.name << ": Encode " << (ok ? "true" : "false (" + encoder.LastError() + ")") << ", " << jpeg.size() << " bytes, decodes to "
			<< decoded.cols << "x" << decoded.rows << ", ends with EOI: "
			<< (jpeg.size() >= 2 && jpeg[jpeg.size() - 2] == 0xFF && jpeg[jpeg.size() - 1] == 0xD9 ? "yes" : "no")
			<< ", frame bytes w*h*1.5 = " << c.frame.size().area() * 3 / 2);
		if (ok) CHECK(decoded.size() == c.frame.size());
		else SUCCEED();
	}
}

// Past the encoder's size limit Encode() also reports success with a JPEG of a few hundred bytes that decodes to garbage.
TEST_CASE("I: a frame larger than the hardware JPEG encoder takes falls back instead of encoding garbage", "[edge][board-media][mjpeg]")
{
	MjpegHwEncoder encoder;
	std::vector<uint8_t> jpeg;
	if (!encoder.Encode(Nv12Picture(640, 480), 80, 1, jpeg)) SKIP("no hardware JPEG encoder here: " + encoder.LastError());
	for (auto [w, h] : { std::pair<int, int>{ 8192, 4320 }, { 8200, 16 }, { 64, 16384 } }) {
		const Frame frame = Nv12Picture(w, h);
		const bool ok = encoder.Encode(frame, 80, 1, jpeg);
		double diff = -1;
		if (ok) {
			cv::Mat decoded = cv::imdecode(jpeg, cv::IMREAD_COLOR);
			diff = MeanAbsDiff(decoded, frame.AsBgr());
		}
		INFO(w << "x" << h << ": Encode " << (ok ? "true" : "false (" + encoder.LastError() + ")") << ", " << jpeg.size() << " bytes, mean abs diff " << diff);
		if (ok) CHECK(diff < 10.0);
		else SUCCEED();
	}
}
