// Median-of-N timings of the OpenCV calls LumenCore makes (resize/letterbox, colour conversion, warp, undistort, PnP, chessboard detection,
// camera calibration, NMS, SGBM), as one JSON object on stdout. Calibration also reports its final RMS so a numeric change shows up.
#include <opencv2/opencv.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/dnn.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace {

struct Timing { double medianMs, minMs, p90Ms; };

Timing Measure(const std::function<void()>& work, int iterations, int warmup = 3)
{
	for (int i = 0; i < warmup; i++) work();
	std::vector<double> ms;
	for (int i = 0; i < iterations; i++) {
		const auto start = std::chrono::steady_clock::now();
		work();
		ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
	}
	std::sort(ms.begin(), ms.end());
	return { ms[ms.size() / 2], ms.front(), ms[static_cast<size_t>(ms.size() * 0.9)] };
}

bool first = true;
void Emit(const std::string& name, const Timing& t, const std::string& extra = "")
{
	std::printf("%s\"%s\":{\"median_ms\":%.4f,\"min_ms\":%.4f,\"p90_ms\":%.4f%s}", first ? "" : ",\n", name.c_str(), t.medianMs, t.minMs, t.p90Ms, extra.c_str());
	first = false;
}

cv::Mat Texture(int rows, int cols, cv::RNG& rng)
{
	cv::Mat m(rows, cols, CV_8UC3);
	rng.fill(m, cv::RNG::UNIFORM, 0, 256);
	cv::GaussianBlur(m, m, cv::Size(0, 0), 2.0);
	return m;
}

// a printed 9x6 board rendered at a pose, for chessboard detection
cv::Mat RenderBoard(cv::Size inner, int square, cv::Size canvas)
{
	cv::Mat board((inner.height + 1) * square, (inner.width + 1) * square, CV_8UC1);
	for (int y = 0; y < board.rows; y++)
		for (int x = 0; x < board.cols; x++) board.at<uchar>(y, x) = (((x / square) + (y / square)) & 1) ? 255 : 0;
	cv::Mat padded;
	cv::copyMakeBorder(board, padded, square, square, square, square, cv::BORDER_CONSTANT, cv::Scalar(255));
	const std::vector<cv::Point2f> from = { { 0, 0 }, { (float)padded.cols, 0 }, { (float)padded.cols, (float)padded.rows }, { 0, (float)padded.rows } };
	const std::vector<cv::Point2f> to = { { 260, 140 }, { (float)canvas.width - 220, 100 }, { (float)canvas.width - 260, (float)canvas.height - 120 }, { 220, (float)canvas.height - 150 } };
	cv::Mat out;
	cv::warpPerspective(padded, out, cv::getPerspectiveTransform(from, to), canvas, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(255));
	return out;
}

}

int main(int argc, char** argv)
{
	const int scale = argc > 1 ? std::max(1, std::atoi(argv[1])) : 1; // multiplies the iteration counts
	cv::RNG rng(1234);
	std::printf("{\"opencv\":\"%s\",\"threads\":%d,\"results\":{\n", CV_VERSION, cv::getNumThreads());

	const cv::Mat frame = Texture(720, 1280, rng);
	{ cv::Mat out; Emit("resize_1280x720_to_640x360_linear", Measure([&] { cv::resize(frame, out, cv::Size(640, 360), 0, 0, cv::INTER_LINEAR); }, 200 * scale)); }
	{
		cv::Mat out;
		Emit("letterbox_1280x720_to_640x640", Measure([&] {
			cv::Mat resized; cv::resize(frame, resized, cv::Size(640, 360));
			cv::copyMakeBorder(resized, out, 140, 140, 0, 0, cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114)); }, 200 * scale));
	}
	{ cv::Mat out; Emit("cvtColor_bgr_to_gray_1280x720", Measure([&] { cv::cvtColor(frame, out, cv::COLOR_BGR2GRAY); }, 300 * scale)); }
	{ cv::Mat out; Emit("cvtColor_bgr_to_yuv_i420_1280x720", Measure([&] { cv::cvtColor(frame, out, cv::COLOR_BGR2YUV_I420); }, 300 * scale)); }
	{
		cv::Mat out; const cv::Mat H = cv::Mat(cv::Matx33d(1, 0.1, 5, 0.05, 1, -3, 1e-5, 2e-5, 1));
		Emit("warpPerspective_1280x720_linear", Measure([&] { cv::warpPerspective(frame, out, H, frame.size(), cv::INTER_LINEAR); }, 100 * scale));
	}
	{
		const cv::Mat K = cv::Mat(cv::Matx33d(900, 0, 640, 0, 900, 360, 0, 0, 1));
		const cv::Mat dist = cv::Mat(cv::Matx<double, 5, 1>(-0.2, 0.05, 0, 0, 0));
		std::vector<cv::Point2d> pts(1000), out;
		for (auto& p : pts) p = { rng.uniform(0.0, 1280.0), rng.uniform(0.0, 720.0) };
		Emit("undistortPoints_1000", Measure([&] { cv::undistortPoints(pts, out, K, dist, cv::noArray(), K); }, 300 * scale));
		const std::vector<cv::Point3d> obj = { { -.08, .08, 0 }, { .08, .08, 0 }, { .08, -.08, 0 }, { -.08, -.08, 0 } };
		const std::vector<cv::Point2d> img = { { 560, 300 }, { 720, 305 }, { 715, 440 }, { 555, 435 } };
		cv::Mat rvec, tvec;
		Emit("solvePnP_ippe_square_4pts", Measure([&] { cv::solvePnP(obj, img, K, dist, rvec, tvec, false, cv::SOLVEPNP_IPPE_SQUARE); }, 2000 * scale));
		std::vector<cv::Point3d> o8 = obj; o8.insert(o8.end(), obj.begin(), obj.end());
		std::vector<cv::Point2d> i8 = img; for (const auto& p : img) i8.push_back(p + cv::Point2d(300, 5));
		Emit("solvePnP_iterative_8pts", Measure([&] { cv::solvePnP(o8, i8, K, dist, rvec, tvec, false, cv::SOLVEPNP_ITERATIVE); }, 1000 * scale));
	}
	{
		const cv::Size inner(9, 6);
		const cv::Mat board = RenderBoard(inner, 60, cv::Size(1280, 720));
		std::vector<cv::Point2f> corners;
		bool found = false;
		const Timing t = Measure([&] {
			found = cv::findChessboardCorners(board, inner, corners, cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_FAST_CHECK); }, 40 * scale);
		Emit("findChessboardCorners_1280x720_9x6", t, std::string(",\"found\":") + (found ? "true" : "false"));
	}
	{
		// 24 synthetic views of a 9x6 board (25 mm squares) under a known camera, with 0.3 px of noise
		const cv::Size inner(9, 6);
		const double square = 0.025, f = 900;
		const cv::Mat K = cv::Mat(cv::Matx33d(f, 0, 640, 0, f, 360, 0, 0, 1));
		const cv::Mat dist = cv::Mat(cv::Matx<double, 5, 1>(-0.18, 0.04, 0.0005, -0.0003, 0));
		std::vector<cv::Point3f> object;
		for (int y = 0; y < inner.height; y++)
			for (int x = 0; x < inner.width; x++) object.emplace_back(static_cast<float>(x * square), static_cast<float>(y * square), 0.f);
		std::vector<std::vector<cv::Point3f>> objects;
		std::vector<std::vector<cv::Point2f>> images;
		cv::RNG views(99);
		while (images.size() < 24) {
			const cv::Vec3d rvec(views.uniform(-0.5, 0.5), views.uniform(-0.5, 0.5), views.uniform(-0.4, 0.4));
			const cv::Vec3d tvec(views.uniform(-0.12, 0.02), views.uniform(-0.1, 0.0), views.uniform(0.45, 0.9));
			std::vector<cv::Point2f> projected;
			cv::projectPoints(object, rvec, tvec, K, dist, projected);
			bool inside = true;
			for (auto& p : projected) {
				p.x += static_cast<float>(views.gaussian(0.3));
				p.y += static_cast<float>(views.gaussian(0.3));
				inside = inside && p.x > 0 && p.x < 1280 && p.y > 0 && p.y < 720;
			}
			if (!inside) continue;
			objects.push_back(object);
			images.push_back(projected);
		}
		double rms = 0;
		const Timing t = Measure([&] {
			cv::Mat cam, d;
			std::vector<cv::Mat> r, tv;
			rms = cv::calibrateCamera(objects, images, cv::Size(1280, 720), cam, d, r, tv); }, 8 * scale, 1);
		char extra[64];
		std::snprintf(extra, sizeof(extra), ",\"rms\":%.6f", rms);
		Emit("calibrateCamera_24_views_9x6", t, extra);
	}
	{
		std::vector<cv::Rect2d> boxes;
		std::vector<float> scores;
		std::vector<int> classes;
		for (int i = 0; i < 400; i++) {
			boxes.emplace_back(rng.uniform(0, 1100), rng.uniform(0, 600), rng.uniform(20, 180), rng.uniform(20, 180));
			scores.push_back(static_cast<float>(rng.uniform(0.3, 1.0)));
			classes.push_back(rng.uniform(0, 3));
		}
		std::vector<int> kept;
		Emit("NMSBoxesBatched_400", Measure([&] { cv::dnn::NMSBoxesBatched(boxes, scores, classes, 0.25f, 0.45f, kept); }, 1000 * scale));
	}
	{
		cv::Mat left = Texture(480, 640, rng), right;
		cv::cvtColor(left, left, cv::COLOR_BGR2GRAY);
		cv::warpAffine(left, right, cv::Mat(cv::Matx23d(1, 0, -12, 0, 1, 0)), left.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
		auto sgbm = cv::StereoSGBM::create(0, 64, 5, 200, 800, 1, 63, 10, 100, 32, cv::StereoSGBM::MODE_SGBM_3WAY);
		cv::Mat disparity;
		Emit("StereoSGBM_3way_640x480_64d", Measure([&] { sgbm->compute(left, right, disparity); }, 10 * scale));
	}
	std::printf("\n}}\n");
	return 0;
}
