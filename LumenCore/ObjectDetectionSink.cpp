#include "ObjectDetectionSink.h"
#include "DistortionCoefficients.h"
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
	m_Calibration = calibration;
	m_CameraMatrix = cv::Mat(cv::Matx<double, 3, 3>(calibration.fx, 0, calibration.cx,
		0, calibration.fy, calibration.cy,
		0, 0, 1));
	// a count of coefficients OpenCV rejects (k1 and k2 alone, say) would make every undistortPoints throw
	const std::vector<double> distortion = NormalisedDistortionCoefficients(calibration.distCoeffs);
	m_DistCoeffs = distortion.empty() ? cv::Mat::zeros(5, 1, CV_64F) : cv::Mat(distortion, true);
}

std::optional<CameraCalibrationResult> ObjectDetectionSink::GetCalibration()
{
	std::lock_guard<std::mutex> lock(m_CalibrationMutex);
	if (!m_HasCalibration) return std::nullopt;
	return m_Calibration;
}

void ObjectDetectionSink::SetTracking(const ObjectTrackingConfig& config)
{
	std::lock_guard<std::mutex> lock(m_TrackingMutex);
	const bool wasEnabled = m_Tracker.Config().enabled;
	m_Tracker.SetConfig(config);
	// switching on starts from no tracks: whatever was tracked before the pause is not the same picture
	if (config.enabled && !wasEnabled) m_Tracker.Reset();
}

ObjectTrackingConfig ObjectDetectionSink::GetTracking()
{
	std::lock_guard<std::mutex> lock(m_TrackingMutex);
	return m_Tracker.Config();
}

void ObjectDetectionSink::Process(const std::vector<SourceResult>& results)
{
	for (const SourceResult& result : results) {
		if (!result.frame.has_value()) continue;

		if (result.frame->empty() || !m_Backend) continue;
		StageLap lap(SinkProfile());
		// the backend takes the frame as it is (a camera's NV12 picture goes to the RGA without a BGR conversion on the way)
		const cv::Size frameSize = result.frame->size();
		std::vector<ObjectDetection> detections = m_Backend->Infer(*result.frame);
		lap.Mark("infer");
		{
			// the backend's own split of "infer"; detail, not extra time
			std::vector<std::pair<std::string, double>> stages;
			m_Backend->LastStageTimingsUs(stages);
			for (const auto& [stage, us] : stages) SinkProfile()->Record(stage, us);
		}

		// identity and velocity for each detection; the tracker is only touched under its mutex, and nothing is added to the output when it is off
		std::vector<TrackInfo> tracks;
		bool tracking = false;
		{
			std::lock_guard<std::mutex> lock(m_TrackingMutex);
			tracking = m_Tracker.Config().enabled;
			if (tracking) tracks = m_Tracker.Update(detections, result.captureTimeUs, frameSize);
		}
		if (tracking) lap.Mark("track");

		// Annotate only when a bound sink wants the frame; otherwise skips the FramePool acquire/copy and drawing.
		bool wantsFrame = HasActiveFrameConsumer();

		// Acquire()+copyTo() rather than .clone(); annotOwner is passed via Frame's pool-owner constructor.
		std::shared_ptr<void> annotOwner;
		cv::Mat annotatedFrame;
		if (wantsFrame) {
			annotatedFrame = FramePool::Instance().Acquire(frameSize.height, frameSize.width, CV_8UC3, annotOwner);
			result.frame->CopyBgrTo(annotatedFrame);
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

		for (size_t index = 0; index < detections.size(); index++) {
			const ObjectDetection& detection = detections[index];
			cv::Rect2d box = detection.GetBoundingBox();

			nlohmann::json entry{
				{"classId", detection.GetClassId()},
				{"className", detection.GetClassName()},
				{"confidence", detection.GetConfidence()},
				{"box", {box.x, box.y, box.width, box.height}},
				{"frameWidth", frameSize.width},
				{"frameHeight", frameSize.height}
			};
			if (tracking) {
				entry["trackId"] = tracks[index].trackId;
				entry["vxPxPerS"] = tracks[index].vxPxPerS;
				entry["vyPxPerS"] = tracks[index].vyPxPerS;
			}
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
				std::string label = (tracking && tracks[index].trackId > 0 ? "#" + std::to_string(tracks[index].trackId) + " " : std::string()) + detection.GetClassName() + " " + std::to_string(static_cast<int>(detection.GetConfidence() * 100)) + "%";
				cv::putText(annotatedFrame, label, cv::Point(static_cast<int>(box.x), static_cast<int>(box.y) - 5),
					cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0xff, 0), 1);
			}
		}

		lap.Mark("json+annotate");

		std::optional<Frame> outputFrame;
		if (wantsFrame) outputFrame = Frame(annotatedFrame, FrameFormat::BGR24, annotOwner);

		SetLatestResult(SourceResult(nlohmann::json(jsonVector), outputFrame, result.captureTimeUs));
		lap.Mark("publish");
	}
}
