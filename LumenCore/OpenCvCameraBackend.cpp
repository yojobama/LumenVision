#include "OpenCvCameraBackend.h"
#include "SourceResult.h"
#include <cctype>
#include <algorithm>
#include <chrono>
#include <future>
#include <thread>
#ifdef _WIN32
#include "WindowsCameraEnumerator.h"
#endif

namespace {
	// cv::VideoCapture's open call can block forever on a misbehaving device, so it runs on a detached worker
	// thread and is abandoned after `timeout`; a stuck thread (and its device handle) is leaked.
	cv::VideoCapture OpenWithTimeout(int index, int backend, std::chrono::milliseconds timeout)
	{
		auto promise = std::make_shared<std::promise<cv::VideoCapture>>();
		std::future<cv::VideoCapture> future = promise->get_future();

		std::thread([index, backend, promise]() {
			promise->set_value(cv::VideoCapture(index, backend));
		}).detach();

		if (future.wait_for(timeout) == std::future_status::ready) {
			return future.get();
		}
		return cv::VideoCapture(); // isOpened() == false
	}
}

bool OpenCvCameraBackend::Open(const std::string& devicePath)
{
#ifdef __linux__
	m_Capture = cv::VideoCapture(devicePath, cv::CAP_V4L2);
#else
	// A plain numeric index (as returned by WindowsCameraEnumerator) needs cv::VideoCapture's int overload, since the
	// string overload does not reliably resolve it on Windows; cv::CAP_MSMF is requested explicitly.
	std::string trimmed = devicePath;
	trimmed.erase(std::remove_if(trimmed.begin(), trimmed.end(), [](unsigned char c) { return std::isspace(c); }), trimmed.end());
	bool isNumericIndex = !trimmed.empty() && std::all_of(trimmed.begin(), trimmed.end(), [](unsigned char c) { return std::isdigit(c); });
	if (isNumericIndex) {
		int index = std::stoi(trimmed);
		// stashed for EnumerateModes(); cv::VideoCapture cannot report which index it was opened with
		m_WindowsDeviceIndex = index;
		// 5s per backend attempt (up to 15s across three attempts)
		constexpr auto kOpenTimeout = std::chrono::seconds(5);
		m_Capture = OpenWithTimeout(index, cv::CAP_MSMF, kOpenTimeout);
		if (!m_Capture.isOpened()) {
			// MSMF may fail on some devices; fall back to DirectShow explicitly
			m_Capture = OpenWithTimeout(index, cv::CAP_DSHOW, kOpenTimeout);
		}
		if (!m_Capture.isOpened()) {
			m_Capture = OpenWithTimeout(index, cv::CAP_ANY, kOpenTimeout);
		}
	} else {
		m_Capture = cv::VideoCapture(devicePath);
	}
#endif
	return m_Capture.isOpened();
}

void OpenCvCameraBackend::Close()
{
	if (m_Capture.isOpened()) m_Capture.release();
}

bool OpenCvCameraBackend::IsOpened() const
{
	return m_Capture.isOpened();
}

CameraGrabResult OpenCvCameraBackend::Grab(bool /*preferGray*/)
{
	CameraGrabResult result;
	if (!m_Capture.isOpened()) return result;
	result.success = m_Capture.read(result.frame);
	// stamped when the read returns, not when SetLatestResult publishes it; stereo pairing gates on the capture instant
	result.captureTimeUs = SourceResult::NowUs();
	return result;
}

std::vector<CameraMode> OpenCvCameraBackend::EnumerateModes()
{
#ifdef _WIN32
	// only meaningful for a device opened by numeric index (see Open()); -1 otherwise
	if (m_WindowsDeviceIndex >= 0) {
		return EnumerateWindowsCameraModes(m_WindowsDeviceIndex, nullptr);
	}
#endif
	return {};
}

bool OpenCvCameraBackend::SetMode(const CameraMode& mode)
{
	if (!m_Capture.isOpened()) return false;
	// order matters: some backends (MSMF) reject a resolution change once FPS is set, so set FPS last
	bool ok = m_Capture.set(cv::CAP_PROP_FRAME_WIDTH, mode.width);
	ok = m_Capture.set(cv::CAP_PROP_FRAME_HEIGHT, mode.height) && ok;
	if (mode.fps > 0.0) ok = m_Capture.set(cv::CAP_PROP_FPS, mode.fps) && ok;
	return ok;
}

CameraMode OpenCvCameraBackend::GetCurrentMode() const
{
	CameraMode mode;
	if (!m_Capture.isOpened()) return mode;
	mode.width = static_cast<int>(m_Capture.get(cv::CAP_PROP_FRAME_WIDTH));
	mode.height = static_cast<int>(m_Capture.get(cv::CAP_PROP_FRAME_HEIGHT));
	mode.fps = m_Capture.get(cv::CAP_PROP_FPS);
	// cv::VideoCapture always returns decoded BGR, so BGR24 is reported regardless of the device's wire format
	mode.pixelFormat = FrameFormat::BGR24;
	return mode;
}

bool OpenCvCameraBackend::SetExposure(int exposureAbsolute)
{
	if (!m_Capture.isOpened()) return false;
	return m_Capture.set(cv::CAP_PROP_EXPOSURE, exposureAbsolute);
}

bool OpenCvCameraBackend::SetAutoExposure(bool enabled)
{
	if (!m_Capture.isOpened()) return false;
	// 3 = aperture priority (auto), 1 = manual (V4L2 convention); Windows backends (MSMF/DSHOW) largely ignore it
	return m_Capture.set(cv::CAP_PROP_AUTO_EXPOSURE, enabled ? 3 : 1);
}

bool OpenCvCameraBackend::SetGain(int gain)
{
	if (!m_Capture.isOpened()) return false;
	return m_Capture.set(cv::CAP_PROP_GAIN, gain);
}
