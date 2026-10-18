#include "RoiSource.h"

RoiSource::RoiSource(std::shared_ptr<Logger> logger, std::string id, cv::Rect roi)
	: ISource(logger, id), ISink(logger, /*maxSources*/ 1, /*requireJson*/ false, /*requireFrame*/ true, id),
	  m_Logger(logger), m_Roi(roi)
{
	m_DoNotLoadCaptureThread = true;
}

void RoiSource::SetRoi(cv::Rect roi)
{
	std::lock_guard<std::mutex> lock(m_RoiMutex);
	m_Roi = roi;
}

cv::Rect RoiSource::GetRoi() const
{
	std::lock_guard<std::mutex> lock(m_RoiMutex);
	return m_Roi;
}

void RoiSource::Process(const std::vector<SourceResult>& results)
{
	const cv::Rect roiNow = GetRoi();
	for (const SourceResult& result : results) {
		if (!result.frame.has_value() || result.frame->empty()) continue;

		cv::Size upstreamSize = result.frame->size();
		cv::Rect bounds(0, 0, upstreamSize.width, upstreamSize.height);
		if ((roiNow & bounds) != roiNow) {
			if (m_Logger) m_Logger->EnterLog(LogLevel::Error,
				"RoiSource: configured ROI does not fit the upstream frame (upstream=" +
				std::to_string(upstreamSize.width) + "x" + std::to_string(upstreamSize.height) + ")");
			continue;
		}

		Frame cropped = result.frame->Roi(roiNow);
		SetLatestResult(SourceResult(std::nullopt, cropped, result.captureTimeUs));
	}
}
