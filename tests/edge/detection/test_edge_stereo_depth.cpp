#include <catch2/catch_test_macros.hpp>
#include "StereoDepthNode.h"
#include "DepthFusionNode.h"
#include "edge_detection_util.h"
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

using namespace edge_detection;

namespace {

constexpr int kW = 640, kH = 480;

std::shared_ptr<Logger> EdgeLogger()
{
	static auto logger = std::make_shared<Logger>("LumenCoreTests-edge-detection.log");
	return logger;
}

std::shared_ptr<StereoDepthNode> MakeNode(const StereoCalibrationResult& calibration, StereoFrameOutput output = STEREO_FRAME_DEPTH_COLORMAP)
{
	auto node = std::make_shared<StereoDepthNode>(EdgeLogger(), "edge-c-stereo", STEREO_BACKEND_SGBM, calibration, 0.2, 20.0, 1000000, output);
	node->SetStereoRoles("L", "R");
	return node;
}

bool HasGrid(const StereoDepthNode& node)
{
	std::vector<float> depth;
	int cols = 0, rows = 0, bw = 0, bh = 0;
	return node.GetLastDepthGrid(depth, cols, rows, bw, bh);
}

} // namespace

// A calibration is only valid at its own resolution and the node enforces that for the LEFT eye (EnsureRectifyMaps), but the right
// eye's frame is never checked: a right camera at a different mode is remapped with maps built for the calibrated size, so most of the
// rectified right image is the black remap border and the depth grid is computed from it and published as if it were good.
TEST_CASE("C: a right eye at a different resolution from the calibration is rejected, not turned into depth", "[edge][detection]") {
	auto node = MakeNode(IdentityStereoCalibration(kW, kH, cv::Rect(), cv::Rect()));
	cv::Mat left, right;
	TexturedPair(kW, kH, 24, left, right);
	cv::Mat smallRight;
	cv::resize(right, smallRight, cv::Size(kW / 2, kH / 2));

	const uint64_t t = SourceResult::NowUs();
	CallProcess(*node, { FrameResult("L", left, t), FrameResult("R", smallRight, t) });

	// the left eye at the wrong size produces no grid (checked below for reference); the right eye at the wrong size must not either
	CHECK_FALSE(HasGrid(*node));

	auto reference = MakeNode(IdentityStereoCalibration(kW, kH, cv::Rect(), cv::Rect()));
	cv::Mat smallLeft;
	cv::resize(left, smallLeft, cv::Size(kW / 2, kH / 2));
	CallProcess(*reference, { FrameResult("L", smallLeft, t), FrameResult("R", right, t) });
	CHECK_FALSE(HasGrid(*reference));
}

// The valid regions come straight from the calibration (the REST body of stereoDepthSink/create, or stereoCalibrations.json) and are
// never intersected with the frame: one that reaches past the image makes m_MapLx(crop) throw in Process(), outside EnsureRectifyMaps'
// try/catch - on the node's thread that terminates the service (X1).
TEST_CASE("C: a calibration whose valid region reaches past the frame does not throw out of Process", "[edge][detection]") {
	const cv::Rect pastTheEdge(100, 0, kW, kH); // x + width = 740 > 640
	auto node = MakeNode(IdentityStereoCalibration(kW, kH, pastTheEdge, pastTheEdge));
	cv::Mat left, right;
	TexturedPair(kW, kH, 24, left, right);
	const uint64_t t = SourceResult::NowUs();
	REQUIRE_NOTHROW(CallProcess(*node, { FrameResult("L", left, t), FrameResult("R", right, t) }));
}

TEST_CASE("C: a calibration whose valid region starts at a negative origin does not throw out of Process", "[edge][detection]") {
	const cv::Rect negative(-32, -16, 320, 240);
	auto node = MakeNode(IdentityStereoCalibration(kW, kH, negative, negative));
	cv::Mat left, right;
	TexturedPair(kW, kH, 24, left, right);
	const uint64_t t = SourceResult::NowUs();
	REQUIRE_NOTHROW(CallProcess(*node, { FrameResult("L", left, t), FrameResult("R", right, t) }));
}

// IsValid() only checks Q and the baseline; EnsureRectifyMaps then reads R1/R2 as 9 doubles and P1/P2 as 12 without checking their
// sizes. A stereoDepthSink/create body (deserialised straight into the native struct) with short arrays reads past the vectors' heap
// blocks (ASan: heap-buffer-overflow); an empty array dereferences a null data pointer. Hidden: under ASan it aborts the process.
TEST_CASE("C: short rectification matrices in a stereo calibration are refused, not read past their end", "[edge][detection]") {
	StereoCalibrationResult calibration = IdentityStereoCalibration(kW, kH, cv::Rect(), cv::Rect());
	calibration.R1 = { 1, 0, 0 };          // 3 of 9
	calibration.P1 = { 600, 0, 320, 0 };   // 4 of 12
	REQUIRE(calibration.IsValid());        // what the node checks before rectifying

	std::shared_ptr<StereoDepthNode> node;
	try {
		node = MakeNode(calibration);
	} catch (const std::exception&) {
		SUCCEED("refused at construction");
		return;
	}
	cv::Mat left, right;
	TexturedPair(kW, kH, 24, left, right);
	const uint64_t t = SourceResult::NowUs();
	REQUIRE_NOTHROW(CallProcess(*node, { FrameResult("L", left, t), FrameResult("R", right, t) }));
	// a malformed calibration must not yield a depth grid computed from whatever followed the vectors in memory
	REQUIRE_FALSE(HasGrid(*node));
}

// DepthFusionNode republishes the detector's result with SourceResult(fused, frame), the two-argument constructor, so the capture time the
// detector carried (the stereo pair's) is replaced by the time fusion ran. NetworkTablesSink stamps every topic with captureTimeUs, so the
// robot sees fused detections as younger than they are and latency compensation uses the wrong time.
TEST_CASE("C: depth fusion keeps the capture time of the detection it fuses", "[edge][detection]") {
	auto depthNode = MakeNode(IdentityStereoCalibration(kW, kH, cv::Rect(), cv::Rect()));
	cv::Mat left, right;
	TexturedPair(kW, kH, 24, left, right);
	const uint64_t captured = SourceResult::NowUs() - 400000; // the pair was captured 400 ms ago
	CallProcess(*depthNode, { FrameResult("L", left, captured), FrameResult("R", right, captured) });
	REQUIRE(HasGrid(*depthNode));

	auto fusion = std::make_shared<DepthFusionNode>(EdgeLogger(), "edge-c-fusion");
	fusion->SetStereoDepthNode(depthNode);

	SourceResult detections(nlohmann::json::array({ { {"className", "note"}, {"box", { 100, 100, 64, 64 }} } }), std::nullopt, captured);
	detections.sourceId = "detector";
	CallProcess(*fusion, { detections });

	SourceResult out = fusion->GetLatestResult();
	REQUIRE(out.json.has_value());
	REQUIRE(out.json->is_array());
	REQUIRE(out.json->at(0).contains("distanceMeters"));
	CHECK(out.captureTimeUs == captured);
}

// SetStereoDepthNode writes the plain shared_ptr m_DepthNode from the REST thread (AttachDepthFusionSource / DetachDepthFusionSource)
// while Process() on the node's thread tests it and then dereferences it several more times: a data race (TSan) and, when a detach lands
// between `if (!m_DepthNode)` and `m_DepthNode->GetDepthGridFor`, a null dereference. Hidden: it may crash the process.
TEST_CASE("C: detaching the depth source while detections flow is safe", "[edge][detection]") {
	auto depthNode = MakeNode(IdentityStereoCalibration(kW, kH, cv::Rect(), cv::Rect()));
	cv::Mat left, right;
	TexturedPair(kW, kH, 24, left, right);
	const uint64_t t = SourceResult::NowUs();
	CallProcess(*depthNode, { FrameResult("L", left, t), FrameResult("R", right, t) });
	REQUIRE(HasGrid(*depthNode));

	auto detector = std::make_shared<PushSource>(EdgeLogger(), "edge-c-detector");
	auto fusion = std::make_shared<DepthFusionNode>(EdgeLogger(), "edge-c-fusion-race");
	REQUIRE(fusion->BindSource(detector));
	fusion->SetStereoDepthNode(depthNode);
	fusion->ISink::Toggle(true);

	std::atomic<bool> stop{ false };
	std::thread publisher([&] {
		while (!stop) {
			detector->Push(SourceResult(nlohmann::json::array({ { {"box", { 10, 10, 50, 50 }} } }), std::nullopt, t));
			std::this_thread::yield();
		}
	});
	auto toggler = std::async(std::launch::async, [&] {
		const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (std::chrono::steady_clock::now() < end) {
			fusion->SetStereoDepthNode(nullptr);   // DetachDepthFusionSource
			fusion->SetStereoDepthNode(depthNode); // AttachDepthFusionSource
		}
	});
	REQUIRE(toggler.wait_for(std::chrono::seconds(20)) == std::future_status::ready);
	stop = true;
	publisher.join();
	fusion->ISink::Toggle(false);
	SUCCEED("no crash; run under TSan for the race on DepthFusionNode::m_DepthNode");
}
