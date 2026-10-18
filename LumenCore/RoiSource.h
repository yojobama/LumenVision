#pragma once
#include "ISink.h"
#include "ISource.h"
#include <mutex>

// Republishes a fixed rectangular crop of one upstream source's frames as its own source, so one side-by-side/
// top-bottom stereo camera can feed StereoCalibrator/StereoDepthNode as two sources.
//
// Uses SourceResult's three-argument constructor to propagate the upstream captureTimeUs, so both halves of one
// exposure report the same captureTimeUs (stereo pairing depends on it).
class RoiSource : public ISink, public ISource
{
public:
	RoiSource(std::shared_ptr<Logger> logger, std::string id, cv::Rect roi);

	// moves the crop while running (the upstream camera changed resolution); the next frame is cut with it
	void SetRoi(cv::Rect roi);
	cv::Rect GetRoi() const;

private:
	void Process(const std::vector<SourceResult>& results) override;

	std::shared_ptr<Logger> m_Logger;
	mutable std::mutex m_RoiMutex;
	cv::Rect m_Roi;
};
