#pragma once
#include "ISource.h"
#include "CameraMode.h"
#include <memory>
#include <string>
#include <vector>

class ICameraBackend;

class CameraFrameSource : public ISource
{
public:
	CameraFrameSource(std::string devicePath, std::shared_ptr<Logger> logger, std::string m_ID);
	CameraFrameSource(std::string devicePath, std::string deviceName, std::shared_ptr<Logger> logger, std::string m_ID);

	~CameraFrameSource();

	std::string getDevicePath();
	std::string getDeviceName();
	void changeDeviceName(std::string newName);

	// Pass-throughs to the ICameraBackend this source opened (V4L2 on Linux, OpenCV elsewhere); m_Backend is private.
	std::vector<CameraMode> GetAvailableModes();
	CameraMode GetCurrentMode();
	bool SetMode(const CameraMode& mode);
	bool SetExposure(int exposureAbsolute);
	bool SetAutoExposure(bool enabled);
	bool SetGain(int gain);
	CameraControlRange GetExposureRange();
	CameraControlRange GetGainRange();
	std::vector<CameraControlInfo> GetControls();
	bool SetControl(int id, int value);

private:
	void CaptureFrame() override;
	std::unique_ptr<ICameraBackend> m_Backend;
	std::string m_DevicePath;
	std::string m_DeviceName;
};
