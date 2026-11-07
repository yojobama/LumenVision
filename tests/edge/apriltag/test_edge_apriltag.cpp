// Edge-case hunt, area B (AprilTag and pose). Each test asserts the correct behaviour, so it fails on the code it was written against.
// Tests that crash or hang the process carry the hidden tag [.] and are run by exact name.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ApriltagDetector.h"
#include "AprilTagFieldLayout.h"
#include "ApriltagFamily.h"
#include "ConstrainedPnp.h"
#include "CoordinateFrames.h"
#include "CpuApriltagBackend.h"
#include "Logger.h"
#ifdef LUMEN_WITH_VULKAN_APRILTAG
#include "VkApriltagBackend.h"
#endif
#include <apriltag/tag36h11.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <mutex>
#include <thread>

namespace {

// a tag36h11 tag, scaled up and padded with white (as test_apriltag_tuning.cpp renders them)
cv::Mat EdgeBRenderTag(int id, int scale = 40, int pad = 60)
{
	apriltag_family_t* family = tag36h11_create();
	image_u8_t* image = apriltag_to_image(family, static_cast<uint32_t>(id));
	cv::Mat cells(image->height, image->width, CV_8UC1, image->buf, image->stride);
	cv::Mat scaled;
	cv::resize(cells, scaled, cv::Size(), scale, scale, cv::INTER_NEAREST);
	cv::Mat padded;
	cv::copyMakeBorder(scaled, padded, pad, pad, pad, pad, cv::BORDER_CONSTANT, cv::Scalar(255));
	image_u8_destroy(image);
	tag36h11_destroy(family);
	return padded;
}

// a source whose picture a test can swap while it runs (frame-size changes, still frames)
class EdgeBFrameSource : public ISource {
public:
	EdgeBFrameSource(std::shared_ptr<Logger> logger, std::string id, cv::Mat gray) : ISource(logger, id) { Set(gray); }
	void Set(const cv::Mat& gray) {
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Gray = gray.clone();
	}
protected:
	void CaptureFrame() override {
		cv::Mat gray;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			gray = m_Gray.clone();
		}
		SetLatestResult(SourceResult(std::nullopt, Frame(gray, FrameFormat::GRAY8)));
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
private:
	std::mutex m_Mutex;
	cv::Mat m_Gray;
};

struct EdgeBRig {
	std::shared_ptr<Logger> logger;
	std::shared_ptr<EdgeBFrameSource> source;
	std::unique_ptr<ApriltagDetector> detector;

	EdgeBRig(const std::string& name, const cv::Mat& gray, CameraCalibrationResult calibration, double tagSize = 0.1651,
		ApriltagBackendKind backend = APRILTAG_BACKEND_CPU, ApriltagTuning tuning = ApriltagTuning())
	{
		logger = std::make_shared<Logger>("LumenCoreTests-edge-apriltag.log");
		source = std::make_shared<EdgeBFrameSource>(logger, name + "-source", gray);
		detector = std::make_unique<ApriltagDetector>(logger, name, calibration, tagSize, backend, 0, 0, tuning);
		// not a Catch2 assertion: some tests build a rig on a worker thread, and Catch2 assertions are for the test's own thread
		if (!detector->BindSource(source)) throw std::runtime_error("EdgeBRig: the detector refused its source");
	}
	void Start() {
		source->Toggle(true);
		static_cast<ISink&>(*detector).Toggle(true);
	}
	void Stop() {
		source->Toggle(false);
		static_cast<ISink&>(*detector).Toggle(false);
	}
	~EdgeBRig() { Stop(); }
	// the detector's latest envelope once at least `ms` has passed
	nlohmann::json LatestAfter(int ms) {
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
		SourceResult result = detector->GetLatestResult();
		REQUIRE(result.json.has_value());
		return *result.json;
	}
};

std::filesystem::path EdgeBWriteFile(const std::string& name, const std::string& text)
{
	const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
	std::ofstream out(path, std::ios::binary);
	out << text;
	return path;
}

std::string EdgeBLayoutTag(int id, double x, double y, double z, double w = 1.0, double qx = 0.0, double qy = 0.0, double qz = 0.0)
{
	return "{\"ID\":" + std::to_string(id) + ",\"pose\":{\"translation\":{\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) +
		",\"z\":" + std::to_string(z) + "},\"rotation\":{\"quaternion\":{\"W\":" + std::to_string(w) + ",\"X\":" + std::to_string(qx) +
		",\"Y\":" + std::to_string(qy) + ",\"Z\":" + std::to_string(qz) + "}}}}";
}

// field-frame corners of a layout tag, exactly as ApriltagDetector::Process builds them
void EdgeBAppendTagCorners(const AprilTagFieldPose& pose, double tagSize, std::vector<cv::Point3d>& out)
{
	for (const auto& c : frames::WpilibTagCorners(tagSize)) {
		cv::Vec3d p = pose.rotation * cv::Vec3d(c[0], c[1], c[2]) + cv::Vec3d(pose.translation.x, pose.translation.y, pose.translation.z);
		out.emplace_back(p[0], p[1], p[2]);
	}
}

const cv::Mat kEdgeBCamera = cv::Mat(cv::Matx<double, 3, 3>(700, 0, 520, 0, 700, 260, 0, 0, 1));

bool EdgeBAllFinite(const nlohmann::json& pose)
{
	for (const char* key : { "x", "y", "z" })
		if (!pose.contains(key) || !pose[key].is_number() || !std::isfinite(pose[key].get<double>())) return false;
	return true;
}

}

// ---------------------------------------------------------------------------------------------------------------------------------
// B1: LoadFieldLayout (REST POST apriltagSink/fieldLayout, fieldLayoutBundled, and SinkManager's ReloadFieldLayout) rebuilds the std::map the
// processing thread reads through TryGetTagPose, with no lock. Run under TSan / ASan; hidden because a freed map node can crash the process.
TEST_CASE("B: reloading a field layout while frames flow does not race the processing thread", "[edge][apriltag][race]") {
	cv::Mat image;
	cv::hconcat(EdgeBRenderTag(3), EdgeBRenderTag(4), image);
	CameraCalibrationResult calibration(700, 700, image.cols / 2.0, image.rows / 2.0, 0.2, {}, image.cols, image.rows);
	EdgeBRig rig("edge-b-layout-race", image, calibration);

	std::string tags;
	for (int id = 1; id <= 40; id++) tags += (id > 1 ? "," : "") + EdgeBLayoutTag(id, 4.0, 0.2 * id, 0.5);
	const auto big = EdgeBWriteFile("edge-b-layout-race-big.json", "{\"tags\":[" + tags + "]}");
	const auto small = EdgeBWriteFile("edge-b-layout-race-small.json",
		"{\"tags\":[" + EdgeBLayoutTag(3, 4.0, 2.0, 0.5) + "," + EdgeBLayoutTag(4, 4.0, 2.5, 0.5) + "]}");
	REQUIRE(rig.detector->LoadFieldLayout(small.string()));
	rig.Start();

	const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	int loads = 0;
	while (std::chrono::steady_clock::now() < until) {
		rig.detector->LoadFieldLayout((loads++ % 2 == 0 ? big : small).string());
	}
	rig.Stop();
	INFO("layout reloads: " << loads);
	REQUIRE(rig.detector->GetLatestResult().json.has_value());
}

// ---------------------------------------------------------------------------------------------------------------------------------
// B2: the detector never checks the calibration's imageWidth/imageHeight against the frame it poses. A calibration made at another
// resolution (camera mode switched in the UI before the 5 s reconciler, an ROI/crop node fed the camera's calibration, a stale saved one)
// produces confidently wrong distances. StereoDepthNode refuses such a calibration; the AprilTag poses should not silently use it.
TEST_CASE("B: a calibration made at another resolution does not give a wrong tag distance", "[edge][apriltag]") {
	const cv::Mat image = EdgeBRenderTag(5); // 520x520
	const double focal = 700.0;
	CameraCalibrationResult matching(focal, focal, image.cols / 2.0, image.rows / 2.0, 0.2, {}, image.cols, image.rows);
	EdgeBRig rig("edge-b-calibration-size", image, matching);
	rig.Start();
	nlohmann::json good = rig.LatestAfter(300);
	REQUIRE(good["tags"].size() == 1);
	REQUIRE(good["tags"][0].contains("pose"));
	const double trueZ = good["tags"][0]["pose"]["z"].get<double>();

	// the same lens calibrated at twice the resolution: every intrinsic doubles
	CameraCalibrationResult doubleSize(2 * focal, 2 * focal, image.cols, image.rows, 0.2, {}, image.cols * 2, image.rows * 2);
	rig.detector->SetCalibration(doubleSize);
	nlohmann::json after = rig.LatestAfter(300);
	REQUIRE(after["tags"].size() == 1);
	INFO("true z " << trueZ << ", envelope " << after["tags"][0].dump());
	// correct: either scale the intrinsics to the frame, or publish no pose for a calibration that does not belong to it
	if (after["tags"][0].contains("pose")) {
		REQUIRE(after["tags"][0]["pose"]["z"].get<double>() == Catch::Approx(trueZ).epsilon(0.05));
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// B3: AprilTagFieldLayout turns the quaternion into a matrix without normalising it (WPILib's Rotation3d(Quaternion) normalises, and so
// does this repo's own frames::QuaternionToRotation). A hand-written or rounded layout gives a non-rotation that skews every multi-tag pose.
TEST_CASE("B: a field layout quaternion that is not unit length still gives a rotation", "[edge][apriltag]") {
	// 90 degrees about Z written unnormalised, as (1, 0, 0, 1)
	const auto path = EdgeBWriteFile("edge-b-layout-quaternion.json", "{\"tags\":[" + EdgeBLayoutTag(7, 1.0, 2.0, 0.5, 1.0, 0.0, 0.0, 1.0) + "]}");
	AprilTagFieldLayout layout;
	REQUIRE(layout.LoadFromFile(path.string()));
	AprilTagFieldPose pose;
	REQUIRE(layout.TryGetTagPose(7, pose));
	const cv::Matx33d expected(0, -1, 0, 1, 0, 0, 0, 0, 1);
	INFO("rotation " << cv::Mat(pose.rotation));
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++) REQUIRE(pose.rotation(r, c) == Catch::Approx(expected(r, c)).margin(1e-9));
}

// ---------------------------------------------------------------------------------------------------------------------------------
// B4: the same layout tag seen twice (a second printed copy on a practice field, a reflection-free duplicate in view) counts as two tags:
// it passes the multi-tag ">= 2 tags" gate and feeds solvePnP two different pixel quads for the same four field points.
TEST_CASE("B: one layout tag seen twice does not yield a multi-tag pose", "[edge][apriltag]") {
	cv::Mat image;
	cv::hconcat(EdgeBRenderTag(3), EdgeBRenderTag(3), image);
	CameraCalibrationResult calibration(700, 700, image.cols / 2.0, image.rows / 2.0, 0.2, {}, image.cols, image.rows);
	EdgeBRig rig("edge-b-duplicate-id", image, calibration);
	const auto path = EdgeBWriteFile("edge-b-layout-duplicate.json", "{\"tags\":[" + EdgeBLayoutTag(3, 4.0, 2.0, 0.5) + "]}");
	REQUIRE(rig.detector->LoadFieldLayout(path.string()));
	rig.Start();
	nlohmann::json envelope = rig.LatestAfter(300);
	REQUIRE(envelope["tags"].size() == 2);
	INFO("multiTag " << envelope["multiTag"].dump());
	// correct: one distinct field tag is not a multi-tag solve (or, if it is solved, it must at least fit the pixels)
	if (!envelope["multiTag"].is_null()) {
		REQUIRE(envelope["multiTag"]["tagCount"].get<int>() < 2);
		REQUIRE(envelope["multiTag"]["reprojErrPixels"].get<double>() < 2.0);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// B5: a field layout with an out-of-range number ("1e400" parses to infinity) is accepted, and the multi-tag solve then throws on the
// sink thread (which terminates the service through X1). Uploadable through POST apriltagSink/fieldLayout.
TEST_CASE("B: a field layout coordinate that overflows a double is rejected or solved without throwing", "[edge][apriltag]") {
	const auto path = EdgeBWriteFile("edge-b-layout-inf.json",
		"{\"tags\":[{\"ID\":1,\"pose\":{\"translation\":{\"x\":1e400,\"y\":1.0,\"z\":0.5},\"rotation\":{\"quaternion\":{\"W\":1,\"X\":0,\"Y\":0,\"Z\":0}}}},"
		+ EdgeBLayoutTag(2, 4.0, 2.0, 0.5) + "]}");
	AprilTagFieldLayout layout;
	layout.LoadFromFile(path.string());
	AprilTagFieldPose first, second;
	if (!layout.TryGetTagPose(1, first)) SUCCEED("the non-finite tag was rejected");
	else {
		INFO("tag 1 translation " << first.translation);
		REQUIRE(layout.TryGetTagPose(2, second));
		std::vector<cv::Point3d> objectPoints;
		EdgeBAppendTagCorners(first, 0.1651, objectPoints);
		EdgeBAppendTagCorners(second, 0.1651, objectPoints);
		std::vector<cv::Point2d> imagePoints = { {100, 300}, {160, 300}, {160, 240}, {100, 240}, {400, 300}, {460, 300}, {460, 240}, {400, 240} };
		nlohmann::json solved;
		REQUIRE_NOTHROW(solved = ApriltagDetector::SolveMultiTagPnP(objectPoints, imagePoints, kEdgeBCamera, cv::Mat::zeros(5, 1, CV_64F), 2));
		if (!solved.is_null()) REQUIRE(EdgeBAllFinite(solved));
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// B6: tag size 0 (POST apriltagSink?tagSize=0 is accepted unchecked) collapses each tag's four field corners onto one point; the multi-tag solve
// on the sink thread must not throw.
TEST_CASE("B: a zero tag size does not make the multi-tag solve throw", "[edge][apriltag]") {
	AprilTagFieldPose a{ cv::Point3d(4.0, 2.0, 0.5), cv::Matx33d::eye() }, b{ cv::Point3d(4.0, 2.5, 0.5), cv::Matx33d::eye() };
	std::vector<cv::Point3d> objectPoints;
	EdgeBAppendTagCorners(a, 0.0, objectPoints);
	EdgeBAppendTagCorners(b, 0.0, objectPoints);
	std::vector<cv::Point2d> imagePoints = { {100, 300}, {160, 300}, {160, 240}, {100, 240}, {400, 300}, {460, 300}, {460, 240}, {400, 240} };
	nlohmann::json solved;
	REQUIRE_NOTHROW(solved = ApriltagDetector::SolveMultiTagPnP(objectPoints, imagePoints, kEdgeBCamera, cv::Mat::zeros(5, 1, CV_64F), 2));
	if (!solved.is_null()) REQUIRE(EdgeBAllFinite(solved));
}

TEST_CASE("B: a detector with a zero tag size keeps running and publishes no non-finite pose", "[edge][apriltag][crash]") {
	cv::Mat image;
	cv::hconcat(EdgeBRenderTag(3), EdgeBRenderTag(4), image);
	CameraCalibrationResult calibration(700, 700, image.cols / 2.0, image.rows / 2.0, 0.2, {}, image.cols, image.rows);
	EdgeBRig rig("edge-b-zero-size", image, calibration, 0.0);
	const auto path = EdgeBWriteFile("edge-b-layout-zero.json",
		"{\"tags\":[" + EdgeBLayoutTag(3, 4.0, 2.0, 0.5) + "," + EdgeBLayoutTag(4, 4.0, 2.5, 0.5) + "]}");
	REQUIRE(rig.detector->LoadFieldLayout(path.string()));
	rig.Start();
	nlohmann::json envelope = rig.LatestAfter(400);
	INFO(envelope.dump());
	REQUIRE(envelope["tags"].size() == 2);
	for (const auto& tag : envelope["tags"])
		if (tag.contains("pose")) REQUIRE(EdgeBAllFinite(tag["pose"]));
	if (!envelope["multiTag"].is_null()) REQUIRE(EdgeBAllFinite(envelope["multiTag"]));
}

// ---------------------------------------------------------------------------------------------------------------------------------
// B7: a calibration whose distortion vector is not one of OpenCV's lengths (4, 5, 8, 12, 14) - e.g. just k1, k2, as the NT e2e test itself
// uses - is accepted by SetCalibration, and cv::undistortPoints then throws on the sink thread for the first tag (terminating via X1).
TEST_CASE("B: a two-coefficient distortion calibration poses a tag instead of throwing", "[edge][apriltag][crash]") {
	const cv::Mat image = EdgeBRenderTag(5);
	CameraCalibrationResult calibration(700, 700, image.cols / 2.0, image.rows / 2.0, 0.2, { 0.01, -0.02 }, image.cols, image.rows);
	EdgeBRig rig("edge-b-dist2", image, calibration);
	rig.Start();
	nlohmann::json envelope = rig.LatestAfter(300);
	REQUIRE(envelope["tags"].size() == 1);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// B8: constrained seed NaN (a robot that publishes an uninitialised pose) - the solve must not report ok with a non-finite position.
TEST_CASE("B: a NaN constrained seed is not reported as a solved pose", "[edge][apriltag]") {
	ConstrainedPnpInput input;
	AprilTagFieldPose tag{ cv::Point3d(4.0, 0.0, 0.5), cv::Matx33d(-1, 0, 0, 0, -1, 0, 0, 0, 1) }; // facing back toward -X
	EdgeBAppendTagCorners(tag, 0.1651, input.fieldPoints);
	input.imagePoints = { {500, 280}, {540, 280}, {540, 240}, {500, 240} };
	input.cameraMatrix = kEdgeBCamera;
	input.distCoeffs = cv::Mat::zeros(5, 1, CV_64F);
	input.seedX = std::numeric_limits<double>::quiet_NaN();
	input.seedY = 0.0;
	input.seedYaw = 0.0;
	ConstrainedPnpResult result;
	REQUIRE_NOTHROW(result = SolveConstrainedPnp(input));
	INFO("ok " << result.ok << " x " << result.x << " y " << result.y << " yaw " << result.yaw);
	if (result.ok) {
		REQUIRE(std::isfinite(result.x));
		REQUIRE(std::isfinite(result.y));
		REQUIRE(std::isfinite(result.yaw));
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Extreme distortion coefficients (a failed calibration that still saved): undistortPoints may diverge; the node must keep publishing
// and never publish a non-finite pose. Run on a detached thread so a hang fails the test instead of blocking it.
TEST_CASE("B: extreme distortion coefficients neither hang the node nor publish a non-finite pose", "[edge][apriltag]") {
	const cv::Mat image = EdgeBRenderTag(5);
	for (const std::vector<double>& coefficients : std::vector<std::vector<double>>{
			{ 50.0, -400.0, 0.5, -0.5, 3000.0 }, { -50.0, 400.0, 0.0, 0.0, -3000.0 }, { 1e6, 1e6, 1e3, 1e3, 1e6 } }) {
		INFO("k1 " << coefficients[0]);
		auto done = std::make_shared<std::promise<std::string>>();
		std::future<std::string> outcome = done->get_future();
		std::thread([image, coefficients, done]() {
			try {
				CameraCalibrationResult calibration(700, 700, image.cols / 2.0, image.rows / 2.0, 0.2, coefficients, image.cols, image.rows);
				EdgeBRig rig("edge-b-extreme-distortion", image, calibration);
				const auto path = EdgeBWriteFile("edge-b-layout-extreme.json", "{\"tags\":[" + EdgeBLayoutTag(5, 4.0, 2.0, 0.5) + "]}");
				rig.detector->LoadFieldLayout(path.string());
				rig.detector->SetConstrainedSeed(2.0, 2.0, 0.0, frames::Pose3());
				rig.Start();
				std::this_thread::sleep_for(std::chrono::milliseconds(400));
				SourceResult result = rig.detector->GetLatestResult();
				rig.Stop();
				if (!result.json) { done->set_value("no envelope"); return; }
				for (const auto& tag : (*result.json)["tags"])
					if (tag.contains("pose") && !EdgeBAllFinite(tag["pose"])) { done->set_value("non-finite pose " + tag.dump()); return; }
				done->set_value("ok " + result.json->dump().substr(0, 200));
			} catch (const std::exception& e) {
				done->set_value(std::string("threw ") + e.what());
			}
		}).detach();
		REQUIRE(outcome.wait_for(std::chrono::seconds(20)) == std::future_status::ready);
		const std::string text = outcome.get();
		INFO(text);
		REQUIRE(text.rfind("ok", 0) == 0);
	}
}

// poseIterations is an unchecked int from the REST query; a huge value must not stall the sink (the solver should converge early).
TEST_CASE("B: a huge pose iteration count still poses a tag promptly", "[edge][apriltag]") {
	const cv::Mat image = EdgeBRenderTag(5);
	CameraCalibrationResult calibration(700, 700, image.cols / 2.0, image.rows / 2.0, 0.2, {}, image.cols, image.rows);
	ApriltagTuning tuning;
	tuning.poseIterations = std::numeric_limits<int>::max();
	EdgeBRig rig("edge-b-iterations", image, calibration, 0.1651, APRILTAG_BACKEND_CPU, tuning);
	rig.Start();
	nlohmann::json envelope = rig.LatestAfter(1500);
	REQUIRE(envelope["tags"].size() == 1);
	REQUIRE(envelope["tags"][0].contains("pose"));
}

// ---------------------------------------------------------------------------------------------------------------------------------
// CPU backend on degenerate frame shapes (a thin ROI crop, a tiny crop): must return, not crash.
// B12: libapriltag's quad search (gradient_clusters / threshold tiles) reads out of bounds for frames a few pixels across. RoiSource accepts
// any crop with a positive size (SetRoiSourceRect only rejects width/height <= 0), so a tiny ROI in front of an AprilTag node kills the service
// with a SIGSEGV - not even an exception. Each size is its own hidden case so one crash does not hide the others.
namespace {
void EdgeBCpuDetectSize(int width, int height, float decimate)
{
	ApriltagTuning tuning;
	tuning.quadDecimate = decimate;
	CpuApriltagBackend backend(tuning);
	cv::Mat frame(height, width, CV_8UC1);
	cv::randu(frame, 0, 255);
	zarray_t* detections = nullptr;
	REQUIRE_NOTHROW(detections = backend.Detect(frame));
	REQUIRE(detections != nullptr);
	backend.ReleaseResult(detections);
}
}
#define EDGE_B_CPU_SIZE(w, h, d) \
	TEST_CASE("B: the CPU backend handles a " #w "x" #h " frame at decimate " #d, "[edge][apriltag][tiny-cpu]") { EdgeBCpuDetectSize(w, h, d); }
EDGE_B_CPU_SIZE(1, 1, 1.0f)
EDGE_B_CPU_SIZE(2, 2, 1.0f)
EDGE_B_CPU_SIZE(3, 3, 1.0f)
EDGE_B_CPU_SIZE(4, 4, 1.0f)
EDGE_B_CPU_SIZE(8, 8, 1.0f)
EDGE_B_CPU_SIZE(16, 16, 1.0f)
EDGE_B_CPU_SIZE(1920, 1, 1.0f)
EDGE_B_CPU_SIZE(1, 1080, 1.0f)
EDGE_B_CPU_SIZE(1920, 2, 1.0f)
EDGE_B_CPU_SIZE(1, 1, 2.0f)
EDGE_B_CPU_SIZE(3, 3, 2.0f)
EDGE_B_CPU_SIZE(8, 8, 2.0f)
EDGE_B_CPU_SIZE(1920, 3, 2.0f)

// the same through the detector node, as a 1x1 ROI crop reaches it
TEST_CASE("B: an AprilTag node fed a 1x1 crop keeps running", "[edge][apriltag][crash]") {
	cv::Mat pixel(1, 1, CV_8UC1, cv::Scalar(128));
	EdgeBRig rig("edge-b-1x1", pixel, CameraCalibrationResult());
	rig.Start();
	nlohmann::json envelope = rig.LatestAfter(300);
	REQUIRE(envelope["tags"].empty());
}

#ifdef LUMEN_WITH_VULKAN_APRILTAG
namespace {
std::unique_ptr<VkApriltagBackend> EdgeBTryVulkan(int width, int height, ApriltagTuning tuning = ApriltagTuning())
{
	try {
		return std::make_unique<VkApriltagBackend>(width, height, tuning);
	} catch (const std::exception&) {
		return nullptr;
	}
}
bool EdgeBHasVulkan()
{
	static const bool has = EdgeBTryVulkan(64, 64) != nullptr;
	return has;
}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// B9: a frame whose pixel count is not a multiple of 4 (any odd-by-odd size, or e.g. 642x481: an ROI crop the user drew, half of a
// side-by-side frame) builds a Vulkan backend - ResolveDecimation even steps down to 1 for it - but GpuDetector sizes the grey buffer as
// (w*h)/4 words, so every Detect() throws "Buffer::Write out of range" (on the sink thread: X1 terminates the service).
TEST_CASE("B: the Vulkan backend detects a tag in a frame whose pixel count is not a multiple of four", "[edge][apriltag][vulkan]") {
	if (!EdgeBHasVulkan()) SKIP("no Vulkan device");
	const cv::Mat tag = EdgeBRenderTag(3, 30); // 420x420
	for (const cv::Size size : { cv::Size(641, 481), cv::Size(642, 481), cv::Size(521, 521) }) {
		INFO("frame " << size);
		cv::Mat frame(size, CV_8UC1, cv::Scalar(255));
		tag.copyTo(frame(cv::Rect(0, 0, tag.cols, tag.rows)));
		ApriltagTuning tuning;
		tuning.quadDecimate = 1.0f;
		auto backend = EdgeBTryVulkan(size.width, size.height, tuning);
		REQUIRE(backend != nullptr);
		zarray_t* detections = nullptr;
		REQUIRE_NOTHROW(detections = backend->Detect(frame));
		REQUIRE(zarray_size(detections) == 1);
	}
}

TEST_CASE("B: a Vulkan detector fed an odd-sized frame keeps running", "[edge][apriltag][vulkan][crash]") {
	if (!EdgeBHasVulkan()) SKIP("no Vulkan device");
	const cv::Mat tag = EdgeBRenderTag(3, 30);
	cv::Mat frame(481, 641, CV_8UC1, cv::Scalar(255));
	tag.copyTo(frame(cv::Rect(0, 0, tag.cols, tag.rows)));
	CameraCalibrationResult calibration(700, 700, frame.cols / 2.0, frame.rows / 2.0, 0.2, {}, frame.cols, frame.rows);
	EdgeBRig rig("edge-b-vk-odd", frame, calibration, 0.1651, APRILTAG_BACKEND_VULKAN);
	rig.Start();
	nlohmann::json envelope = rig.LatestAfter(1000);
	REQUIRE(envelope["tags"].size() == 1);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// B10: frames smaller than the GPU pipeline can work on (decimated side < 3: interior_width_ = decimated_width_ - 2 underflows in uint32).
// Must either be refused at construction (so the detector falls back) or detect nothing - never crash or hang. Hidden: may crash/hang the GPU.
TEST_CASE("B: tiny frames on the Vulkan backend are refused or return cleanly", "[edge][apriltag][vulkan][tiny]") {
	if (!EdgeBHasVulkan()) SKIP("no Vulkan device");
	const std::vector<std::pair<cv::Size, float>> cases = {
		{ {2, 2}, 1.0f }, { {4, 4}, 2.0f }, { {4, 4}, 1.0f }, { {8, 2}, 2.0f }, { {1920, 2}, 1.0f }, { {2, 1080}, 1.0f }, { {6, 6}, 2.0f },
	};
	for (const auto& [size, decimate] : cases) {
		INFO("frame " << size << " decimate " << decimate);
		auto future = std::async(std::launch::async, [size = size, decimate = decimate]() -> std::string {
			ApriltagTuning tuning;
			tuning.quadDecimate = decimate;
			std::unique_ptr<VkApriltagBackend> backend;
			try {
				backend = std::make_unique<VkApriltagBackend>(size.width, size.height, tuning);
			} catch (const std::exception& e) {
				return std::string("refused: ") + e.what();
			}
			cv::Mat frame(size, CV_8UC1);
			cv::randu(frame, 0, 255);
			try {
				zarray_t* detections = backend->Detect(frame);
				return "detected " + std::to_string(zarray_size(detections));
			} catch (const std::exception& e) {
				return std::string("Detect threw: ") + e.what();
			}
		});
		REQUIRE(future.wait_for(std::chrono::seconds(20)) == std::future_status::ready);
		const std::string outcome = future.get();
		UNSCOPED_INFO("frame " << size << " decimate " << decimate << ": " << outcome);
		// a backend that was built must be able to run its frame
		REQUIRE(outcome.rfind("Detect threw", 0) != 0);
	}
	SUCCEED();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// B11: one frame size the Vulkan backend cannot be built for sets m_VulkanUnavailable for good: when the frames return to a normal size the
// detector stays on the CPU backend for the rest of the process (a missing GPU and an unusable frame size are treated alike).
TEST_CASE("B: a Vulkan detector returns to the GPU after one frame size it could not build for", "[edge][apriltag][vulkan]") {
	if (!EdgeBHasVulkan()) SKIP("no Vulkan device");
	const cv::Mat normal = EdgeBRenderTag(3); // 520x520
	// 8193x101: odd, so decimation 1, and 2*8193 > 16383 - GpuDetector refuses it at construction (a deterministic stand-in for any build
	// failure); tall enough that the CPU fallback itself can run it (see B12)
	const cv::Mat unusable(101, 8193, CV_8UC1, cv::Scalar(255));
	REQUIRE(EdgeBTryVulkan(unusable.cols, unusable.rows) == nullptr);

	CameraCalibrationResult none;
	EdgeBRig rig("edge-b-vk-rebuild", normal, none, 0.1651, APRILTAG_BACKEND_VULKAN);
	rig.Start();
	rig.LatestAfter(800);
	REQUIRE(rig.detector->GetBackendKind() == APRILTAG_BACKEND_VULKAN);

	rig.source->Set(unusable);
	rig.LatestAfter(500);
	rig.source->Set(normal);
	nlohmann::json envelope = rig.LatestAfter(1000);
	REQUIRE(envelope["tags"].size() == 1);
	INFO("backend now " << rig.detector->GetBackendName());
	REQUIRE(rig.detector->GetBackendKind() == APRILTAG_BACKEND_VULKAN);
}

// tags touching the frame border on the GPU path (refine-edges and decode sample next to the edge): must not read outside the frame (ASan)
TEST_CASE("B: the Vulkan backend handles a tag cut by the frame border", "[edge][apriltag][vulkan]") {
	if (!EdgeBHasVulkan()) SKIP("no Vulkan device");
	const cv::Mat tag = EdgeBRenderTag(3, 20, 0); // 200x200, no white margin
	for (const cv::Point offset : { cv::Point(0, 0), cv::Point(-10, -10), cv::Point(440, 280), cv::Point(450, 290) }) {
		INFO("tag at " << offset);
		cv::Mat frame(480, 640, CV_8UC1, cv::Scalar(255));
		cv::Rect dst(offset, tag.size());
		cv::Rect clipped = dst & cv::Rect(0, 0, frame.cols, frame.rows);
		tag(cv::Rect(clipped.x - dst.x, clipped.y - dst.y, clipped.width, clipped.height)).copyTo(frame(clipped));
		for (RefineEdgesMode mode : { REFINE_UPSTREAM, REFINE_EXACT, REFINE_FAST, REFINE_ULTRAFAST }) {
			ApriltagTuning tuning;
			tuning.refineMode = mode;
			tuning.quadDecimate = 1.0f;
			auto backend = EdgeBTryVulkan(frame.cols, frame.rows, tuning);
			REQUIRE(backend != nullptr);
			REQUIRE_NOTHROW(backend->Detect(frame));
		}
	}
}
#endif
