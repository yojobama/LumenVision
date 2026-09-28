#pragma once

#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <memory>
#include <cstdint>
#include "Frame.h"

class SourceResult
{
public:
	SourceResult();
	// Frame's constructor is implicit from cv::Mat (BGR24), so a bare cv::Mat argument converts to std::optional<Frame>
	// through this single overload; a parallel optional<cv::Mat> overload would make such calls ambiguous.
	SourceResult(std::optional<nlohmann::json> json, std::optional<Frame> frame);
	// as above, plus an explicit capture timestamp for producers that know when the frame was captured;
	// needed to pair frames from two independent sources (StereoCalibrator/StereoDepthNode)
	SourceResult(std::optional<nlohmann::json> json, std::optional<Frame> frame, uint64_t captureTimeUs);
	std::optional<nlohmann::json> json;
	std::optional<Frame> frame;

	// which bound source this result came from; filled in by ISink::ProcessingThreadLoop, not by the producer
	std::string sourceId;

	// filled in by ISource::SetLatestResult: monotonic per-source sequence number, starting at 1
	uint64_t frameNumber = 0;

	// wall-clock microseconds since epoch when the frame was captured, and when SetLatestResult published it;
	// the two-argument constructor uses producedTimeUs for both
	uint64_t captureTimeUs = 0;
	uint64_t producedTimeUs = 0;

	// current wall-clock time in microseconds since epoch; the single clock for every timestamp here, so sources are comparable
	static uint64_t NowUs();
};
