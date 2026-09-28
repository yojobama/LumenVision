#include "VideoFileSource.h"
#include "ImageFileSource.h"
#include <chrono>
#include <thread>

VideoFileFrameSource::VideoFileFrameSource(std::shared_ptr<Logger> logger, std::string filePath, int fps, std::string m_ID) : ISource(logger, m_ID)
{
	if (logger) logger->EnterLog("VideoFileFrameSource constructed with filePath: " + filePath);
	logger->EnterLog(LogLevel::Info, "initializing a video file capture device");
	this->m_Capture = cv::VideoCapture(filePath);
	if (!m_Capture.isOpened()) throw "unable to open video file: " + filePath;
    this->m_Fps = fps;
}

void VideoFileFrameSource::CaptureFrame()
{
    if (m_Capture.isOpened()) {
        m_Logger->EnterLog(LogLevel::Info, "camera is open, grabbing frame and returning it");

        cv::Mat mat;
        // Grab to advance and then sleep to respect the configured FPS
        m_Capture.grab();
        int delayMs = (m_Fps > 0) ? (1000 / m_Fps) : 33;
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

        if (m_Capture.read(mat) && !mat.empty()) {
            SetLatestResult(SourceResult(std::nullopt, mat));
        } else {
            m_Logger->EnterLog(LogLevel::Error, "Failed to read frame from video file");
        }
    } else {
        m_Logger->EnterLog(LogLevel::Error, "camera is closed, cannot capture frame");
    }
}
