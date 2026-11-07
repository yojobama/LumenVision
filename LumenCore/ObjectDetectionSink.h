#pragma once
#include "ISink.h"
#include "ISource.h"
#include "IDetectionBackend.h"
#include "CameraCalibrationResult.h"
#include "ObjectTracker.h"
#include <memory>
#include <mutex>

class ObjectDetectionSink : public ISink, public ISource
{
public:
	// takes ownership of an already-Load()-ed backend; the sink does not know which concrete backend it holds
	ObjectDetectionSink(std::shared_ptr<Logger> logger, std::string id, std::shared_ptr<IDetectionBackend> backend);

	// which backend (RKNN or ONNX Runtime) this sink runs, chosen at construction from the model's file format
	std::string GetBackendName() const { return m_Backend ? m_Backend->Name() : "none"; }

	// Changes the confidence and NMS cutoffs of the running model (see IDetectionBackend::SetThresholds).
	void SetThresholds(float confThreshold, float nmsThreshold) { if (m_Backend) m_Backend->SetThresholds(confThreshold, nmsThreshold); }
	float GetConfThreshold() const { return m_Backend ? m_Backend->GetConfThreshold() : 0.0f; }
	float GetNmsThreshold() const { return m_Backend ? m_Backend->GetNmsThreshold() : 0.0f; }

	// Tracking (off by default): each detection then also carries a trackId (-1 until the object has been seen minHits times in a row) and the
	// velocity of its box centre in pixels per second. Changing the settings keeps the tracks; turning tracking on starts from none.
	void SetTracking(const ObjectTrackingConfig& config);
	ObjectTrackingConfig GetTracking();

	// With a calibration each detection also reports the yaw/pitch of its box centre (degrees, positive left / up, as the
	// AprilTag targets); without one they are omitted.
	void SetCalibration(const CameraCalibrationResult& calibration);
	// the calibration given with SetCalibration, or none; NetworkTablesSink publishes it as this node's cameraIntrinsics
	std::optional<CameraCalibrationResult> GetCalibration() override;

private:
	void Process(const std::vector<SourceResult>& results) override;

	std::shared_ptr<IDetectionBackend> m_Backend;
	std::shared_ptr<Logger> m_Logger;

	std::mutex m_TrackingMutex; // guards the tracker (processing thread) against SetTracking/GetTracking (request threads)
	ObjectTracker m_Tracker;

	std::mutex m_CalibrationMutex;
	bool m_HasCalibration = false;
	CameraCalibrationResult m_Calibration;
	cv::Mat m_CameraMatrix;
	cv::Mat m_DistCoeffs;
};
