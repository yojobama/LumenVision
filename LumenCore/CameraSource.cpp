#include "CameraSource.h"
#include "Frame.h"
#include "OpenCvCameraBackend.h"
#ifdef __linux__
#include "V4l2CameraBackend.h"
#endif

namespace {
	// AUTO: V4L2 on Linux, falling back to OpenCvCameraBackend if the device won't open through V4L2;
	// OpenCvCameraBackend on other platforms.
	std::unique_ptr<ICameraBackend> OpenAutoBackend(const std::string& devicePath, std::shared_ptr<Logger> logger)
	{
#ifdef __linux__
		auto v4l2Backend = std::make_unique<V4l2CameraBackend>();
		if (v4l2Backend->Open(devicePath)) return v4l2Backend;
		if (logger) logger->EnterLog(LogLevel::Warning, "V4L2 backend failed to open " + devicePath + ", falling back to OpenCV");
#endif
		auto openCvBackend = std::make_unique<OpenCvCameraBackend>();
		openCvBackend->Open(devicePath);
		return openCvBackend;
	}
}

CameraFrameSource::CameraFrameSource(std::string devicePath, std::shared_ptr<Logger> logger, std::string id) : ISource(logger, id)
{
    m_Backend = OpenAutoBackend(devicePath, logger);
    if (!m_Backend->IsOpened()) throw "unable to open webcam: " + devicePath;

    this->m_DevicePath = devicePath;
    this->m_DeviceName = getDeviceName();
}

CameraFrameSource::CameraFrameSource(std::string devicePath, std::string deviceName, std::shared_ptr<Logger> logger, std::string id) : ISource(logger, id)
{
    m_Backend = OpenAutoBackend(devicePath, logger);
    if (!m_Backend->IsOpened()) {
        if (logger) logger->EnterLog(LogLevel::Error, "unable to open webcam: " + devicePath);
    }
    this->m_DeviceName = deviceName;
    this->m_DevicePath = devicePath;
}

CameraFrameSource::~CameraFrameSource()
{
}

std::string CameraFrameSource::getDevicePath()
{
    return m_DevicePath;
}

std::string CameraFrameSource::getDeviceName()
{
    return m_DeviceName;
}

void CameraFrameSource::changeDeviceName(std::string newName)
{
    this->m_DeviceName = newName;
}

std::vector<CameraMode> CameraFrameSource::GetAvailableModes()
{
    return m_Backend->EnumerateModes();
}

CameraMode CameraFrameSource::GetCurrentMode()
{
    return m_Backend->GetCurrentMode();
}

bool CameraFrameSource::SetMode(const CameraMode& mode)
{
    return m_Backend->SetMode(mode);
}

bool CameraFrameSource::SetExposure(int exposureAbsolute)
{
    return m_Backend->SetExposure(exposureAbsolute);
}

bool CameraFrameSource::SetAutoExposure(bool enabled)
{
    return m_Backend->SetAutoExposure(enabled);
}

bool CameraFrameSource::SetGain(int gain)
{
    return m_Backend->SetGain(gain);
}

CameraControlRange CameraFrameSource::GetExposureRange()
{
    return m_Backend->GetExposureRange();
}

CameraControlRange CameraFrameSource::GetGainRange()
{
    return m_Backend->GetGainRange();
}

std::vector<CameraControlInfo> CameraFrameSource::GetControls()
{
    return m_Backend->EnumerateControls();
}

bool CameraFrameSource::SetControl(int id, int value)
{
    return m_Backend->SetControl(id, value);
}

void CameraFrameSource::SetTransform(const FrameTransform& transform)
{
    std::lock_guard<std::mutex> lock(m_TransformMutex);
    m_Transform = transform;
}

FrameTransform CameraFrameSource::GetTransform()
{
    std::lock_guard<std::mutex> lock(m_TransformMutex);
    return m_Transform;
}

void CameraFrameSource::CaptureFrame()
{
    if (m_Backend->IsOpened()) {
        // No bound sink needs colour: ask the backend to decode straight to grayscale. Uses
        // HasActiveColorFrameConsumer(), as a bound detector needs a frame but not colour.
        CameraGrabResult grab = m_Backend->Grab(!HasActiveColorFrameConsumer());
        if (grab.success) {
            // Carries grab.poolOwner explicitly: the bare-cv::Mat overload has no pool owner, so the buffer
            // could be recycled while sinks still use it (see CameraGrabResult::poolOwner).
            cv::Mat image = grab.frame;
            std::shared_ptr<void> owner = grab.poolOwner;
            const FrameTransform transform = GetTransform();
            if (!transform.IsIdentity()) {
                bool viewOfInput = true;
                image = transform.Apply(image, viewOfInput);
                // a transformed image is a fresh copy, so it no longer needs the pooled buffer
                if (!viewOfInput) owner.reset();
            }
            SetLatestResult(SourceResult(std::nullopt, Frame(image, grab.format, owner), grab.captureTimeUs));
        } else {
            m_Logger->EnterLog(LogLevel::Error, "camera grab failed for " + m_DevicePath);
        }
    } else {
        m_Logger->EnterLog(LogLevel::Error, "camera is closed, not capturing a frame");
    }
}
