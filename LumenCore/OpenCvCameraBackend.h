#pragma once
#include "ICameraBackend.h"

// The always-available fallback backend: cv::VideoCapture.
class OpenCvCameraBackend : public ICameraBackend
{
public:
	bool Open(const std::string& devicePath) override;
	void Close() override;
	bool IsOpened() const override;
	// preferGray is ignored: cv::VideoCapture cannot request a direct-to-gray decode
	CameraGrabResult Grab(bool preferGray = false) override;
	std::string Name() const override { return "OpenCV"; }

	// cv::VideoCapture cannot list supported modes; on Windows this queries Media Foundation
	// (WindowsCameraEnumerator.cpp) instead.
	std::vector<CameraMode> EnumerateModes() override;
	bool SetMode(const CameraMode& mode) override;
	CameraMode GetCurrentMode() const override;
	bool SetExposure(int exposureAbsolute) override;
	bool SetAutoExposure(bool enabled) override;
	bool SetGain(int gain) override;

private:
	cv::VideoCapture m_Capture;
#ifdef _WIN32
	// set by Open() for a numeric devicePath (also used by OpenWithTimeout) so EnumerateModes() can query Media
	// Foundation for the same device; -1 if not opened by numeric index
	int m_WindowsDeviceIndex = -1;
#endif
};
