#include "ObjectDetectionSink.h"
#include "FramePool.h"
#include <nlohmann/json.hpp>

ObjectDetectionSink::ObjectDetectionSink(std::shared_ptr<Logger> logger, std::string id, std::shared_ptr<IDetectionBackend> backend)
	: ISource(logger, id), ISink(logger, 1, false, true, id), m_Backend(backend), m_Logger(logger)
{
	if (m_Logger) m_Logger->EnterLog("ObjectDetectionSink constructed with backend=" + (backend ? backend->Name() : "none"));
	m_DoNotLoadCaptureThread = true;
}

void ObjectDetectionSink::Process(const std::vector<SourceResult>& results)
{
	for (const SourceResult& result : results) {
		if (!result.frame.has_value()) continue;

		if (result.frame->empty() || !m_Backend) continue;
		const cv::Mat& sourceFrame = result.frame->AsBgr();

		if (m_DriverMode) {
			// AsBgrFrame() rather than the bare cv::Mat, to keep FramePool ownership tracking.
			SetLatestResult(SourceResult(nlohmann::json(std::vector<nlohmann::json>{}), result.frame->AsBgrFrame(), result.captureTimeUs));
			continue;
		}

		std::vector<ObjectDetection> detections = m_Backend->Infer(sourceFrame);

		// Annotate only when a bound sink wants the frame; otherwise skips the FramePool acquire/copy and drawing.
		bool wantsFrame = HasActiveFrameConsumer();

		// Acquire()+copyTo() rather than .clone(); annotOwner is passed via Frame's pool-owner constructor.
		std::shared_ptr<void> annotOwner;
		cv::Mat annotatedFrame;
		if (wantsFrame) {
			annotatedFrame = FramePool::Instance().Acquire(sourceFrame.rows, sourceFrame.cols, sourceFrame.type(), annotOwner);
			sourceFrame.copyTo(annotatedFrame);
		}
		std::vector<nlohmann::json> jsonVector;

		for (const ObjectDetection& detection : detections) {
			cv::Rect2d box = detection.GetBoundingBox();

			jsonVector.push_back(nlohmann::json{
				{"classId", detection.GetClassId()},
				{"className", detection.GetClassName()},
				{"confidence", detection.GetConfidence()},
				{"box", {box.x, box.y, box.width, box.height}}
			});

			if (wantsFrame) {
				cv::rectangle(annotatedFrame, box, cv::Scalar(0, 0xff, 0), 2);
				std::string label = detection.GetClassName() + " " + std::to_string(static_cast<int>(detection.GetConfidence() * 100)) + "%";
				cv::putText(annotatedFrame, label, cv::Point(static_cast<int>(box.x), static_cast<int>(box.y) - 5),
					cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0xff, 0), 1);
			}
		}

		std::optional<Frame> outputFrame;
		if (wantsFrame) outputFrame = Frame(annotatedFrame, FrameFormat::BGR24, annotOwner);

		SetLatestResult(SourceResult(nlohmann::json(jsonVector), outputFrame, result.captureTimeUs));
	}
}
