#include "ObjectDetectionSink.h"
#include "FramePool.h"
#include <nlohmann/json.hpp>
#include <opencv2/calib3d.hpp>
#include <cmath>

// a Windows header pulled in by OpenCV defines GetClassName as GetClassNameA, which would rename ObjectDetection::GetClassName
#ifdef GetClassName
#undef GetClassName
#endif

ObjectDetectionSink::ObjectDetectionSink(std::shared_ptr<Logger> logger, std::string id, std::shared_ptr<IDetectionBackend> backend)
	: ISource(logger, id), ISink(logger, 1, false, true, id), m_Backend(backend), m_Logger(logger)
{
	if (m_Logger) m_Logger->EnterLog("ObjectDetectionSink constructed with backend=" + (backend ? backend->Name() : "none"));
	m_DoNotLoadCaptureThread = true;
}

void ObjectDetectionSink::SetCalibration(const CameraCalibrationResult& calibration)
{
	std::lock_guard<std::mutex> lock(m_CalibrationMutex);
	m_HasCalibration = calibration.fx > 0.0 && calibration.fy > 0.0;
	if (!m_HasCalibration) return;
	m_CameraMatrix = (cv::Mat_<double>(3, 3) <<
		calibration.fx, 0, calibration.cx,
		0, calibration.fy, calibration.cy,
		0, 0, 1);
	m_DistCoeffs = calibration.HasDistortion() ? cv::Mat(calibration.distCoeffs, true) : cv::Mat::zeros(5, 1, CV_64F);
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

		cv::Mat cameraMatrix, distCoeffs;
		bool hasCalibration;
		{
			std::lock_guard<std::mutex> lock(m_CalibrationMutex);
			hasCalibration = m_HasCalibration;
			if (hasCalibration) {
				cameraMatrix = m_CameraMatrix;
				distCoeffs = m_DistCoeffs;
			}
		}

		for (const ObjectDetection& detection : detections) {
			cv::Rect2d box = detection.GetBoundingBox();

			nlohmann::json entry{
				{"classId", detection.GetClassId()},
				{"className", detection.GetClassName()},
				{"confidence", detection.GetConfidence()},
				{"box", {box.x, box.y, box.width, box.height}},
				{"frameWidth", sourceFrame.cols},
				{"frameHeight", sourceFrame.rows}
			};
			if (hasCalibration) {
				// normalised image coordinates of the box centre (lens distortion removed): yaw = atan(-x) (positive left), pitch = atan(-y), as for tags
				std::vector<cv::Point2d> centre{ {box.x + box.width / 2.0, box.y + box.height / 2.0} };
				std::vector<cv::Point2d> normalised;
				cv::undistortPoints(centre, normalised, cameraMatrix, distCoeffs);
				entry["yawDeg"] = std::atan(-normalised[0].x) * 180.0 / CV_PI;
				entry["pitchDeg"] = std::atan(-normalised[0].y) * 180.0 / CV_PI;
			}
			jsonVector.push_back(entry);

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
