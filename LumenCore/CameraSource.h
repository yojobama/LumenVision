#pragma once
#include <chrono>
#include <optional>
#include "ISource.h"
#include "CameraMode.h"
#include "FrameTransform.h"
#include <mutex>
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

	// Reshapes every frame this source publishes (see FrameTransform.h); an identity transform removes it.
	void SetTransform(const FrameTransform& transform);
	FrameTransform GetTransform();

private:
	void CaptureFrame() override;
	std::unique_ptr<ICameraBackend> m_Backend;
	std::string m_DevicePath;
	std::string m_DeviceName;
	std::mutex m_TransformMutex;
	FrameTransform m_Transform;

	// The camera could not be opened (or went away): the capture thread then retries every couple of seconds instead of spinning, and
	// puts the last mode that was applied back once the device is open again.
	bool m_ClosedLogged = false;
	int m_RetrySeconds = 2; // grows to 10 while the device stays away
	// A camera that is unplugged can go on reporting itself open while every read fails (Media Foundation and V4L2 both do). Failed grabs are
	// paced, logged once a second at most, and after a couple of seconds of them the device is closed so the retry branch above reopens it.
	int m_GrabFailures = 0;
	std::chrono::steady_clock::time_point m_LastGrabFailureLogged{};
	std::mutex m_ModeMutex;
	std::optional<CameraMode> m_LastMode;
};
