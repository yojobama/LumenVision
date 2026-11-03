// Edge-case hunt H (board-npu): RknnDetectionBackend, RgaLetterbox, RgaColorConverter and how ObjectDetectionSink / YoloPostProcess use them.
// Board only (Orange Pi 5 Plus, RK3588). The odd-shaped models under models/ are made by models/make_models.py (rknn-toolkit2 2.5.0, WSL).
// Needs an RK3588 and the models in LUMEN_RKNN_MODEL_DIR (tests that need one skip without it); the small synthetic models are in models/.
#include <catch2/catch_test_macros.hpp>
#include "YoloPostProcess.h"
#include "Frame.h"
#include <opencv2/opencv.hpp>
#include <unistd.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef LUMEN_WITH_RKNN
#include "RknnDetectionBackend.h"
#endif
#ifdef LUMEN_WITH_RGA
#include "RgaLetterbox.h"
#if defined(LUMEN_WITH_WEBRTC) || defined(LUMEN_WITH_RECORD)
#include "RgaColorConverter.h"
#define H_WITH_RGA_CONVERTER 1
extern "C" {
#include <libavutil/frame.h>
}
#endif
#endif

namespace fs = std::filesystem;

namespace {

std::string EdgeModel(const char* name) { return (fs::path(__FILE__).parent_path() / "models" / name).string(); }

std::string Yolo11Model()
{
	const char* dir = std::getenv("LUMEN_RKNN_MODEL_DIR");
	if (!dir) return {};
	const std::string path = std::string(dir) + "/yolo11n-coco-int8.rknn";
	return fs::exists(path) ? path : std::string{};
}

bool OnRk3588()
{
	std::ifstream f("/proc/device-tree/compatible", std::ios::binary);
	std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	return s.find("rk3588") != std::string::npos;
}

int CountFds()
{
	int n = 0;
	for (const auto& e : fs::directory_iterator("/proc/self/fd")) { (void)e; n++; }
	return n;
}

int CountMaps()
{
	std::ifstream f("/proc/self/maps");
	std::string line;
	int n = 0;
	while (std::getline(f, line)) n++;
	return n;
}

std::string TempFile(const std::string& name, const std::vector<char>& bytes)
{
	const std::string path = (fs::temp_directory_path() / ("h-edge-" + std::to_string(::getpid()) + "-" + name)).string();
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!bytes.empty()) f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	return path;
}

std::vector<char> ReadAll(const std::string& path)
{
	std::ifstream f(path, std::ios::binary);
	return std::vector<char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// a smooth picture with colour and some low-frequency structure, so a wrong offset, stride or scale shows up as a large difference
cv::Mat SmoothBgr(int width, int height)
{
	cv::Mat m(height, width, CV_8UC3);
	for (int r = 0; r < height; r++)
		for (int c = 0; c < width; c++) {
			const double fx = static_cast<double>(c) / std::max(1, width), fy = static_cast<double>(r) / std::max(1, height);
			m.at<cv::Vec3b>(r, c) = cv::Vec3b(static_cast<uchar>(30 + 190 * fx), static_cast<uchar>(30 + 190 * fy),
				static_cast<uchar>(128 + 90 * std::sin(6.2831853 * 2 * fx) * std::cos(6.2831853 * 2 * fy)));
		}
	return m;
}

// an NV12 frame whose planes sit in one buffer with the given luma row stride (as the hardware decoder lays them out)
Frame SmoothNv12(int width, int height, int stride, int hstride = 0)
{
	if (hstride == 0) hstride = height;
	auto buf = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(stride) * hstride * 3 / 2 + 64, 0);
	cv::Mat y(height, width, CV_8UC1, buf->data(), static_cast<size_t>(stride));
	cv::Mat uv(height / 2, width / 2, CV_8UC2, buf->data() + static_cast<size_t>(stride) * hstride, static_cast<size_t>(stride));
	for (int r = 0; r < height; r++)
		for (int c = 0; c < width; c++)
			y.at<uchar>(r, c) = static_cast<uchar>(30 + 190 * c / std::max(1, width) + 20 * std::sin(6.2831853 * 2 * r / std::max(1, height)));
	for (int r = 0; r < height / 2; r++)
		for (int c = 0; c < width / 2; c++)
			uv.at<cv::Vec2b>(r, c) = cv::Vec2b(static_cast<uchar>(96 + 64 * r / std::max(1, height / 2)), static_cast<uchar>(160 - 64 * c / std::max(1, width / 2)));
	return Frame::FromNv12(y, uv, buf);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------------------
// RKNN: models of an unexpected shape and files that are not models
// ---------------------------------------------------------------------------------------------------------------------------------------------
#ifdef LUMEN_WITH_RKNN

namespace {
DetectionBackendConfig ConfigFor(const std::string& path)
{
	DetectionBackendConfig config;
	config.modelPath = path;
	config.labelsPath = std::string(LUMEN_TEST_DATA_DIR) + "/coco_80_labels.txt";
	config.confThreshold = 0.25f;
	config.nmsThreshold = 0.45f;
	return config;
}
}

// rknn_model_zoo's YOLOv5 export has three outputs ([1,255,S/8,S/8], [1,255,S/16,S/16], [1,255,S/32,S/32]). 3 % 3 == 0, so Load takes it for one
// "scale" of the un-fused DFL layout: box = output 0 (grid S/8), class = output 1 (grid S/16), and decodes 255 class rows of (S/8)^2 cells from a
// tensor that only holds (S/16)^2 cells per row.
TEST_CASE("H: a YOLOv5-shaped .rknn (three outputs of different grids) is refused at load", "[edge][board-npu]") {
	if (!OnRk3588()) SKIP("not an RK3588");
	for (const char* name : { "yolov5shape_320_fp16.rknn", "yolov5shape_320_i8.rknn" }) {
		INFO(name);
		RknnDetectionBackend backend;
		CHECK_FALSE(backend.Load(ConfigFor(EdgeModel(name))));
	}
}

// (the decode of such a model, which read past its class tensor, can no longer be reached: it is refused above)

TEST_CASE("H: a model whose input is not an image is refused at load, or at least Infer does not throw", "[edge][board-npu]") {
	if (!OnRk3588()) SKIP("not an RK3588");
	RknnDetectionBackend backend;
	const bool loaded = backend.Load(ConfigFor(EdgeModel("vector_input_fused_fp16.rknn")));
	CHECK_FALSE(loaded);
	if (loaded) {
		cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
		CHECK_NOTHROW(backend.Infer(bus));
	}
}

// rknn_init(ctx, model, size, ...) reads `model` as a file path when size is 0; an empty file gives size 0 and an empty vector's data(), nullptr.
TEST_CASE("H: an empty .rknn file is refused at load", "[edge][board-npu]") {
	if (!OnRk3588()) SKIP("not an RK3588");
	const std::string path = TempFile("empty.rknn", {});
	RknnDetectionBackend backend;
	const bool loaded = backend.Load(ConfigFor(path));
	fs::remove(path);
	REQUIRE_FALSE(loaded);
}

TEST_CASE("H: files that are not rknn models (garbage or a JPEG or a cut-short model) are refused at load", "[edge][board-npu]") {
	if (!OnRk3588()) SKIP("not an RK3588");
	const std::string yolo = Yolo11Model();
	if (yolo.empty()) SKIP("yolo11n-coco-int8.rknn is not in LUMEN_RKNN_MODEL_DIR");
	std::vector<char> model = ReadAll(yolo);
	std::mt19937 rng(1);
	std::vector<char> garbage(64 * 1024);
	for (char& c : garbage) c = static_cast<char>(rng());
	struct Case { const char* name; std::vector<char> bytes; };
	std::vector<Case> cases = {
		{ "one-byte", { 'R' } },
		{ "garbage-64k", garbage },
		{ "jpeg", ReadAll(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg") },
		{ "header-only-64", std::vector<char>(model.begin(), model.begin() + 64) },
		{ "header-only-4k", std::vector<char>(model.begin(), model.begin() + 4096) },
		{ "first-half", std::vector<char>(model.begin(), model.begin() + model.size() / 2) },
	};
	// the model with its weights zeroed after the header: well-formed metadata, nonsense body
	{
		std::vector<char> zeroedBody = model;
		std::fill(zeroedBody.begin() + std::min<size_t>(zeroedBody.size(), 64 * 1024), zeroedBody.end(), 0);
		cases.push_back({ "zeroed-body", zeroedBody });
	}
	for (const Case& c : cases) {
		INFO(c.name);
		std::fprintf(stderr, "H: loading %s (%zu bytes)\n", c.name, c.bytes.size());
		const std::string path = TempFile(std::string(c.name) + ".rknn", c.bytes);
		RknnDetectionBackend backend;
		bool loaded = false;
		// Load() reports a bad model by returning false; it must not throw (librknnrt's rknn_init throws its JSON parser's exception for a
		// model with corrupt metadata, and leaks what it allocated)
		CHECK_NOTHROW(loaded = backend.Load(ConfigFor(path)));
		fs::remove(path);
		if (loaded) {
			// a model that loads must still answer Infer without failing
			cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
			CHECK_NOTHROW(backend.Infer(bus));
		}
		CHECK_FALSE(loaded);
	}
}

// The yolo11n model ends in about 3.6 kB of metadata after the section its header sizes (exportDataSize). A file cut short inside that tail - an
// upload that lost its last bytes - passes librknnrt's size check, and rknn_init then memcpy()s the tail from past the end of the buffer
// LumenCore read the file into (exactly the file's size, nothing after it).
TEST_CASE("H: a model missing its last bytes is refused at load without reading past the file", "[edge][board-npu]") {
	if (!OnRk3588()) SKIP("not an RK3588");
	const std::string yolo = Yolo11Model();
	if (yolo.empty()) SKIP("yolo11n-coco-int8.rknn is not in LUMEN_RKNN_MODEL_DIR");
	const std::vector<char> model = ReadAll(yolo);
	const char* env = std::getenv("H_CUT_BYTES");
	const size_t cut = env ? std::strtoul(env, nullptr, 10) : 1;
	std::fprintf(stderr, "H: loading the model without its last %zu bytes (%zu of %zu)\n", cut, model.size() - cut, model.size());
	const std::string path = TempFile("cut.rknn", std::vector<char>(model.begin(), model.end() - static_cast<long>(cut)));
	RknnDetectionBackend backend;
	const bool loaded = backend.Load(ConfigFor(path));
	fs::remove(path);
	CHECK_FALSE(loaded);
}

// A crop 1920x1 (the web UI accepts any crop of at least 1x1) letterboxes to a 640x0 picture: the RGA refuses it, and the CPU fallback's
// cv::resize to a zero height throws out of Infer() - ObjectDetectionSink::Process and ISink's thread do not catch it.
TEST_CASE("H: Infer on a frame far wider than tall (or taller than wide) does not throw", "[edge][board-npu]") {
	if (!OnRk3588()) SKIP("not an RK3588");
	const std::string yolo = Yolo11Model();
	if (yolo.empty()) SKIP("yolo11n-coco-int8.rknn is not in LUMEN_RKNN_MODEL_DIR");
	struct Size { int w, h; bool nv12; };
	for (const Size s : { Size{ 1920, 1, false }, Size{ 1, 1920, false }, Size{ 4000, 2, true }, Size{ 2, 4000, true }, Size{ 1281, 1, false } }) {
		INFO(s.w << "x" << s.h << (s.nv12 ? " NV12" : " BGR"));
		RknnDetectionBackend backend;
		REQUIRE(backend.Load(ConfigFor(yolo)));
		if (s.nv12) {
			const Frame f = SmoothNv12(s.w, s.h, s.w);
			CHECK_NOTHROW(backend.Infer(f));
		} else {
			cv::Mat m = SmoothBgr(s.w, s.h);
			CHECK_NOTHROW(backend.Infer(m));
		}
	}
}

namespace {
bool LastInferUsedRga(const RknnDetectionBackend& backend)
{
	std::vector<std::pair<std::string, double>> stages;
	backend.LastStageTimingsUs(stages);
	for (const auto& st : stages)
		if (st.first == "rknn-rga") return true;
	return false;
}
}

// 848x480 (a common UVC mode) letterboxes to 640x362 at y=139: the odd offset makes RgaLetterbox decline, and the backend then prepares every
// later frame on the CPU (with an NV12 -> BGR conversion first), even frames the RGA handles - one such frame turns the RGA off for good.
TEST_CASE("H: one frame the RGA declines does not turn the RGA off for the frames after it", "[edge][board-npu]") {
	if (!OnRk3588()) SKIP("not an RK3588");
	const std::string yolo = Yolo11Model();
	if (yolo.empty()) SKIP("yolo11n-coco-int8.rknn is not in LUMEN_RKNN_MODEL_DIR");
	RknnDetectionBackend backend;
	REQUIRE(backend.Load(ConfigFor(yolo)));
	const Frame hd = SmoothNv12(1280, 720, 1280);
	const Frame uvc = SmoothNv12(848, 480, 848);
	(void)backend.Infer(hd);
	REQUIRE(LastInferUsedRga(backend));
	(void)backend.Infer(uvc);
	INFO("848x480 used the RGA: " << LastInferUsedRga(backend));
	(void)backend.Infer(hd);
	CHECK(LastInferUsedRga(backend));
}

// The same frame, prepared by the RGA and by the CPU, must give the same detections, whatever its size, stride or crop.
TEST_CASE("H: the RGA and CPU input paths find the same objects for odd sizes, strides and crops", "[edge][board-npu]") {
	if (!OnRk3588()) SKIP("not an RK3588");
	const std::string yolo = Yolo11Model();
	if (yolo.empty()) SKIP("yolo11n-coco-int8.rknn is not in LUMEN_RKNN_MODEL_DIR");
	cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
	REQUIRE_FALSE(bus.empty());

	struct Variant { std::string name; cv::Mat bgr; };
	std::vector<Variant> variants;
	for (const cv::Size s : { cv::Size(810, 1080), cv::Size(1000, 562), cv::Size(641, 855), cv::Size(1366, 768), cv::Size(250, 333), cv::Size(3000, 4000) }) {
		cv::Mat r;
		cv::resize(bus, r, s);
		variants.push_back({ std::to_string(s.width) + "x" + std::to_string(s.height), r });
	}
	cv::Mat wide;
	cv::copyMakeBorder(bus, wide, 0, 0, 7, 9, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
	variants.push_back({ "roi x=7 of a wider picture", wide(cv::Rect(7, 0, bus.cols, bus.rows)) });
	variants.push_back({ "roi x=1 y=3 w=599", bus(cv::Rect(1, 3, 599, bus.rows - 3)) });

	for (const Variant& v : variants) {
		INFO(v.name);
		::setenv("LUMEN_RGA_DISABLE", "1", 1);
		RknnDetectionBackend cpu;
		REQUIRE(cpu.Load(ConfigFor(yolo)));
		::unsetenv("LUMEN_RGA_DISABLE");
		RknnDetectionBackend rga;
		REQUIRE(rga.Load(ConfigFor(yolo)));
		const auto a = cpu.Infer(v.bgr);
		const auto b = rga.Infer(v.bgr);
		std::vector<std::pair<std::string, double>> stages;
		rga.LastStageTimingsUs(stages);
		bool usedRga = false;
		for (const auto& st : stages) usedRga = usedRga || st.first == "rknn-rga";
		INFO("rga used: " << usedRga << ", cpu " << a.size() << " detections, rga " << b.size());
		CHECK(a.size() == b.size());
		for (const ObjectDetection& d : a) {
			double best = 0;
			for (const ObjectDetection& e : b) {
				if (e.GetClassId() != d.GetClassId()) continue;
				const cv::Rect2d i = d.GetBoundingBox() & e.GetBoundingBox();
				const double u = d.GetBoundingBox().area() + e.GetBoundingBox().area() - i.area();
				best = std::max(best, u > 0 ? i.area() / u : 0.0);
			}
			INFO(d.GetClassName() << " " << d.GetBoundingBox());
			CHECK(best > 0.8);
		}
	}
}

// The int8 outputs decoded directly must find what the runtime's float conversion finds, at every threshold the web UI allows.
TEST_CASE("H: int8 and float decoding of the same model agree", "[edge][board-npu]") {
	if (!OnRk3588()) SKIP("not an RK3588");
	const std::string yolo = Yolo11Model();
	if (yolo.empty()) SKIP("yolo11n-coco-int8.rknn is not in LUMEN_RKNN_MODEL_DIR");
	cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
	::setenv("LUMEN_RKNN_FLOAT_OUTPUT", "1", 1);
	RknnDetectionBackend f;
	REQUIRE(f.Load(ConfigFor(yolo)));
	::unsetenv("LUMEN_RKNN_FLOAT_OUTPUT");
	RknnDetectionBackend q;
	REQUIRE(q.Load(ConfigFor(yolo)));
	for (const float conf : { 0.01f, 0.1f, 0.25f, 0.5f, 0.9f, 0.99f, 1.0f }) {
		for (const float nms : { 0.01f, 0.45f, 1.0f }) {
			f.SetThresholds(conf, nms);
			q.SetThresholds(conf, nms);
			const auto a = f.Infer(bus), b = q.Infer(bus);
			INFO("conf " << conf << " nms " << nms << ": float " << a.size() << ", int8 " << b.size());
			const double tol = std::max<double>(2.0, a.size() * 0.05);
			CHECK(std::abs(static_cast<double>(a.size()) - static_cast<double>(b.size())) <= tol);
		}
	}
}

// Every Load()/destroy cycle must hand back what rknn_init took: file descriptors (the NPU's DRM device, dma-bufs), mappings and memory. The first
// round warms the allocators up; the later rounds must not grow.
namespace {
long RssKb()
{
	std::ifstream f("/proc/self/status");
	std::string line;
	while (std::getline(f, line))
		if (line.rfind("VmRSS:", 0) == 0) return std::stol(line.substr(6));
	return 0;
}
}
TEST_CASE("H: loading and destroying a model many times leaks no file descriptors, mappings or memory", "[edge][board-npu]") {
	if (!OnRk3588()) SKIP("not an RK3588");
	const std::string yolo = Yolo11Model();
	if (yolo.empty()) SKIP("yolo11n-coco-int8.rknn is not in LUMEN_RKNN_MODEL_DIR");
	cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
	auto round = [&](int n) {
		for (int i = 0; i < n; i++) {
			RknnDetectionBackend backend;
			REQUIRE(backend.Load(ConfigFor(yolo)));
			REQUIRE(backend.Load(ConfigFor(yolo))); // a reload on the same object
			(void)backend.Infer(bus);
		}
	};
	round(20);
	const int fds0 = CountFds(), maps0 = CountMaps();
	const long rss0 = RssKb();
	round(40);
	const int fds1 = CountFds(), maps1 = CountMaps();
	const long rss1 = RssKb();
	round(40);
	const int fds2 = CountFds(), maps2 = CountMaps();
	const long rss2 = RssKb();
	INFO("fds " << fds0 << " -> " << fds1 << " -> " << fds2 << ", maps " << maps0 << " -> " << maps1 << " -> " << maps2
		<< ", rss kB " << rss0 << " -> " << rss1 << " -> " << rss2);
	std::fprintf(stderr, "H leak: fds %d -> %d -> %d, maps %d -> %d -> %d, rss kB %ld -> %ld -> %ld\n", fds0, fds1, fds2, maps0, maps1, maps2, rss0, rss1, rss2);
	CHECK(fds2 <= fds0);
	CHECK(maps2 <= maps0 + 4);
	CHECK(rss2 - rss1 < 8 * 1024);
}

// Several detectors on their own threads at once (the core mask hands them out round-robin); under TSan this looks for races between them.
TEST_CASE("H: eight detectors on eight threads at once all answer", "[edge][board-npu]") {
	if (!OnRk3588()) SKIP("not an RK3588");
	const std::string yolo = Yolo11Model();
	if (yolo.empty()) SKIP("yolo11n-coco-int8.rknn is not in LUMEN_RKNN_MODEL_DIR");
	cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
	const Frame nv12 = SmoothNv12(1280, 720, 1344);
	std::atomic<int> loaded{ 0 }, answered{ 0 };
	std::vector<std::thread> threads;
	for (int t = 0; t < 8; t++) {
		threads.emplace_back([&, t] {
			RknnDetectionBackend backend;
			if (!backend.Load(ConfigFor(yolo))) return;
			loaded++;
			bool ok = true;
			for (int i = 0; i < 6; i++) {
				const auto d = (t % 2) ? backend.Infer(nv12) : backend.Infer(bus);
				if (t % 2 == 0 && d.size() < 3) ok = false;
			}
			if (ok) answered++;
		});
	}
	for (auto& th : threads) th.join();
	CHECK(loaded == 8);
	CHECK(answered == 8);
}

#endif // LUMEN_WITH_RKNN

// ---------------------------------------------------------------------------------------------------------------------------------------------
// RGA: the letterbox and the BGR -> NV12 converter against the CPU, for sizes, strides and crops the hardware has rules about
// ---------------------------------------------------------------------------------------------------------------------------------------------
#ifdef LUMEN_WITH_RGA

namespace {
// RgaLetterbox's output against the CPU path's (LetterboxInto + BGR2RGB) for the same frame; "" when they agree or the RGA declined
std::string CompareLetterbox(const Frame& frame, int canvasW, int canvasH)
{
	YoloPostProcess::LetterboxInfo info;
	const cv::Rect picture = YoloPostProcess::LetterboxLayout(frame.size().width, frame.size().height, canvasW, canvasH, info);
	cv::Mat canvas(canvasH, canvasW, CV_8UC3, cv::Scalar(1, 2, 3));
	RgaLetterbox rga;
	if (!rga.Run(frame, canvas.data, canvasW, canvasH, picture)) return "";
	cv::Mat lb, ref;
	YoloPostProcess::LetterboxInto(frame.AsBgr(), canvasW, canvasH, info, lb);
	cv::cvtColor(lb, ref, cv::COLOR_BGR2RGB);
	cv::Mat diff;
	cv::absdiff(canvas, ref, diff);
	cv::Mat inside = diff(picture);
	const cv::Scalar m = cv::mean(inside);
	const double mean = (m[0] + m[1] + m[2]) / 3.0;
	cv::Mat borderMask(canvasH, canvasW, CV_8UC1, cv::Scalar(255));
	borderMask(picture).setTo(0);
	cv::Mat gray;
	cv::cvtColor(diff, gray, cv::COLOR_RGB2GRAY);
	const int badBorder = cv::countNonZero(gray & borderMask);
	std::ostringstream o;
	if (mean > 6.0 || badBorder > 0) o << "mean |rga-cpu| in picture " << mean << ", border pixels off " << badBorder << " (picture " << picture << ")";
	return o.str();
}
}

TEST_CASE("H: RgaLetterbox either declines or matches the CPU letterbox, for BGR frames of odd sizes, strides and crops", "[edge][board-npu]") {
	if (!OnRk3588()) SKIP("not an RK3588");
	std::vector<std::pair<std::string, cv::Mat>> frames;
	for (const cv::Size s : { cv::Size(1280, 720), cv::Size(1920, 1080), cv::Size(641, 481), cv::Size(1000, 562), cv::Size(1366, 768),
			cv::Size(810, 1080), cv::Size(100, 100), cv::Size(70, 70), cv::Size(66, 66), cv::Size(34, 34), cv::Size(2, 2), cv::Size(1, 1),
			cv::Size(4096, 2160), cv::Size(6, 1000), cv::Size(1000, 6), cv::Size(3, 640), cv::Size(720, 1280), cv::Size(8192, 4320) })
		frames.push_back({ std::to_string(s.width) + "x" + std::to_string(s.height), SmoothBgr(s.width, s.height) });
	cv::Mat big = SmoothBgr(1920, 1080);
	frames.push_back({ "roi 1,1 1278x718", big(cv::Rect(1, 1, 1278, 718)) });
	frames.push_back({ "roi 3,0 1000x562", big(cv::Rect(3, 0, 1000, 562)) });
	frames.push_back({ "roi 0,0 1918x1080", big(cv::Rect(0, 0, 1918, 1080)) });
	frames.push_back({ "roi 902,500 1018x580 (bottom right)", big(cv::Rect(902, 500, 1018, 580)) });
	for (const auto& [name, mat] : frames) {
		for (const cv::Size canvas : { cv::Size(640, 640), cv::Size(640, 384), cv::Size(320, 320), cv::Size(600, 600), cv::Size(648, 360) }) {
			INFO(name << " into " << canvas);
			CHECK(CompareLetterbox(Frame(mat), canvas.width, canvas.height) == "");
		}
	}
}

TEST_CASE("H: RgaLetterbox either declines or matches the CPU letterbox, for NV12 frames of odd strides and crops", "[edge][board-npu]") {
	if (!OnRk3588()) SKIP("not an RK3588");
	struct N { int w, h, stride, hstride; };
	std::vector<std::pair<std::string, Frame>> frames;
	for (const N n : { N{ 1280, 720, 1280, 720 }, N{ 1280, 720, 1344, 736 }, N{ 1920, 1080, 1920, 1088 }, N{ 1366, 768, 1366, 768 },
			N{ 1366, 768, 1376, 768 }, N{ 850, 480, 850, 480 }, N{ 642, 482, 642, 482 }, N{ 66, 66, 66, 66 }, N{ 2, 2, 2, 2 }, N{ 4096, 2160, 4096, 2160 } })
		frames.push_back({ std::to_string(n.w) + "x" + std::to_string(n.h) + " stride " + std::to_string(n.stride) + "/" + std::to_string(n.hstride),
			SmoothNv12(n.w, n.h, n.stride, n.hstride) });
	const Frame big = SmoothNv12(1920, 1080, 1920, 1088);
	frames.push_back({ "roi 2,2 1276x716", big.Roi(cv::Rect(2, 2, 1276, 716)) });
	frames.push_back({ "roi 640,360 1280x720 (bottom right)", big.Roi(cv::Rect(640, 360, 1280, 720)) });
	frames.push_back({ "roi 960,0 960x1080 (right half)", big.Roi(cv::Rect(960, 0, 960, 1080)) });
	frames.push_back({ "roi 0,540 1920x540 (bottom half)", big.Roi(cv::Rect(0, 540, 1920, 540)) });
	for (const auto& [name, frame] : frames) {
		for (const cv::Size canvas : { cv::Size(640, 640), cv::Size(640, 384), cv::Size(600, 600), cv::Size(648, 360) }) {
			INFO(name << (frame.IsNv12() ? " (NV12)" : " (BGR)") << " into " << canvas);
			CHECK(CompareLetterbox(frame, canvas.width, canvas.height) == "");
		}
	}
}

#ifdef H_WITH_RGA_CONVERTER
TEST_CASE("H: RgaColorConverter either declines or matches the CPU BGR->NV12 conversion, for odd sizes and crops", "[edge][board-npu]") {
	if (!OnRk3588()) SKIP("not an RK3588");
	std::vector<std::pair<std::string, cv::Mat>> frames;
	for (const cv::Size s : { cv::Size(640, 480), cv::Size(642, 480), cv::Size(650, 482), cv::Size(1000, 562), cv::Size(1366, 768),
			cv::Size(100, 100), cv::Size(64, 64), cv::Size(641, 481), cv::Size(2, 2), cv::Size(1920, 1080) })
		frames.push_back({ std::to_string(s.width) + "x" + std::to_string(s.height), SmoothBgr(s.width, s.height) });
	cv::Mat big = SmoothBgr(1920, 1080);
	frames.push_back({ "roi 1,1 1278x718", big(cv::Rect(1, 1, 1278, 718)) });
	frames.push_back({ "roi 640,360 1280x720 (bottom right)", big(cv::Rect(640, 360, 1280, 720)) });
	for (const auto& [name, bgr] : frames) {
		INFO(name);
		AVFrame* f = av_frame_alloc();
		f->format = AV_PIX_FMT_NV12;
		f->width = bgr.cols;
		f->height = bgr.rows;
		REQUIRE(av_frame_get_buffer(f, 32) == 0);
		RgaColorConverter conv;
		const bool ok = conv.ConvertBgrToNv12(bgr, f);
		if (ok) {
			// BT.601 limited range, as libswscale's default
			cv::Mat i420;
			cv::cvtColor(bgr, i420, cv::COLOR_BGR2YUV_I420);
			const int w = bgr.cols, h = bgr.rows;
			cv::Mat yRef = i420(cv::Rect(0, 0, w, h));
			cv::Mat yGot(h, w, CV_8UC1, f->data[0], static_cast<size_t>(f->linesize[0]));
			cv::Mat d;
			cv::absdiff(yRef, yGot, d);
			const double yMean = cv::mean(d)[0];
			INFO("Y mean diff " << yMean);
			CHECK(yMean < 4.0);
		}
		av_frame_free(&f);
	}
}
#endif

#endif // LUMEN_WITH_RGA
