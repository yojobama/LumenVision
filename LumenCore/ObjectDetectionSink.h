#pragma once
#include "ISink.h"
#include "ISource.h"
#include "IDetectionBackend.h"
#include "CameraCalibrationResult.h"
#include <memory>
#include <mutex>

class ObjectDetectionSink : public ISink, public ISource
{
public:
	// takes ownership of an already-Load()-ed backend; the sink does not know which concrete backend it holds
	ObjectDetectionSink(std::shared_ptr<Logger> logger, std::string id, std::shared_ptr<IDetectionBackend> backend);

	// which backend (RKNN or ONNX Runtime) this sink runs, chosen at construction from the model's file format
	std::string GetBackendName() const { return m_Backend ? m_Backend->Name() : "none"; }

	// Driver mode: skips inference and keeps streaming raw video.
	void SetDriverMode(bool enabled) { m_DriverMode = enabled; }
	bool GetDriverMode() const { return m_DriverMode; }

	// With a calibration each detection also reports the yaw/pitch of its box centre (degrees, positive right / up, as the
	// AprilTag targets); without one they are omitted.
	void SetCalibration(const CameraCalibrationResult& calibration);

private:
	void Process(const std::vector<SourceResult>& results) override;

	std::shared_ptr<IDetectionBackend> m_Backend;
	std::shared_ptr<Logger> m_Logger;
	bool m_DriverMode = false;

	std::mutex m_CalibrationMutex;
	bool m_HasCalibration = false;
	cv::Mat m_CameraMatrix;
	cv::Mat m_DistCoeffs;
};
