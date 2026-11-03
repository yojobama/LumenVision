#pragma once
// Helpers for the area-C (detection / stereo / depth / calibration) edge-case tests.
#include "ISink.h"
#include "ISource.h"
#include "SourceResult.h"
#include "StereoCalibrationResult.h"
#include <opencv2/opencv.hpp>
#include <vector>

namespace edge_detection {
namespace {

// ISink::Process is protected (and private in every node). Explicit template instantiation is exempt from access checks, which lets a test
// name the member and call a node's Process() synchronously on the test thread: an exception it throws then fails the test instead of
// terminating the whole test process from the node's own thread (X1).
struct ProcessTag {
	using type = void (ISink::*)(const std::vector<SourceResult>&);
	friend type GetProcessMember(ProcessTag);
};
template <typename Tag, typename Tag::type Member>
struct AccessRobber {
	friend typename Tag::type GetProcessMember(Tag) { return Member; }
};
template struct AccessRobber<ProcessTag, &ISink::Process>;

inline void CallProcess(ISink& sink, const std::vector<SourceResult>& results)
{
	(sink.*GetProcessMember(ProcessTag{}))(results);
}

inline SourceResult FrameResult(const std::string& sourceId, const cv::Mat& frame, uint64_t captureTimeUs)
{
	SourceResult r(std::nullopt, frame, captureTimeUs);
	r.sourceId = sourceId;
	return r;
}

// identity rectification (both eyes the same pinhole camera, no rotation), as tests/test_stereo_depth_crop.cpp builds it
inline StereoCalibrationResult IdentityStereoCalibration(int width, int height, cv::Rect roiLeft, cv::Rect roiRight)
{
	StereoCalibrationResult c;
	const double f = 600.0, cx = width / 2.0, cy = height / 2.0, baseline = 0.1;
	c.left = CameraCalibrationResult(f, f, cx, cy, 0.1, std::vector<double>(5, 0.0), width, height);
	c.right = c.left;
	c.R = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	c.T = { -baseline, 0, 0 };
	c.R1 = c.R;
	c.R2 = c.R;
	c.P1 = { f, 0, cx, 0, 0, f, cy, 0, 0, 0, 1, 0 };
	c.P2 = { f, 0, cx, -f * baseline, 0, f, cy, 0, 0, 0, 1, 0 };
	c.Q = { 1, 0, 0, -cx, 0, 1, 0, -cy, 0, 0, 0, f, 0, 0, 1 / baseline, 0 };
	c.baselineMeters = baseline;
	c.rectifiedFx = f;
	c.rectifiedCx = cx;
	c.rectifiedCy = cy;
	c.imageWidth = width;
	c.imageHeight = height;
	c.roiLeftX = roiLeft.x; c.roiLeftY = roiLeft.y; c.roiLeftW = roiLeft.width; c.roiLeftH = roiLeft.height;
	c.roiRightX = roiRight.x; c.roiRightY = roiRight.y; c.roiRightW = roiRight.width; c.roiRightH = roiRight.height;
	return c;
}

// blocky random texture, shifted by `shift` px between the eyes, so SGBM has something to match
inline void TexturedPair(int width, int height, int shift, cv::Mat& left, cv::Mat& right, int seed = 7)
{
	cv::RNG rng(seed);
	const int texel = 8;
	cv::Mat coarse((height + texel - 1) / texel, (width + shift + texel - 1) / texel, CV_8UC1);
	rng.fill(coarse, cv::RNG::UNIFORM, 0, 255);
	cv::Mat base;
	cv::resize(coarse, base, cv::Size(width + shift, height), 0, 0, cv::INTER_NEAREST);
	cv::Mat l = base(cv::Rect(shift, 0, width, height)).clone();
	cv::Mat r = base(cv::Rect(0, 0, width, height)).clone();
	cv::cvtColor(l, left, cv::COLOR_GRAY2BGR);
	cv::cvtColor(r, right, cv::COLOR_GRAY2BGR);
}

// a source a test publishes into by hand
class PushSource : public ISource {
public:
	PushSource(std::shared_ptr<Logger> logger, std::string id) : ISource(logger, id) { m_DoNotLoadCaptureThread = true; }
	void Push(SourceResult result) { SetLatestResult(std::move(result)); }
};

} // namespace
} // namespace edge_detection
