#include "VideoFileSource.h"
#include "ImageFileSource.h"
#include <chrono>
#include <thread>

VideoFileFrameSource::VideoFileFrameSource(std::shared_ptr<Logger> logger, std::string filePath, int fps, std::string m_ID) : ISource(logger, m_ID)
{
	if (logger) logger->EnterLog("VideoFileFrameSource constructed with filePath: " + filePath);
	logger->EnterLog(LogLevel::Info, "initialising a video file capture device");
	this->m_Capture = cv::VideoCapture(filePath);
	if (!m_Capture.isOpened()) throw "unable to open video file: " + filePath;
    this->m_Fps = fps;
}

void VideoFileFrameSource::CaptureFrame()
{
    if (!m_Capture.isOpened()) {
        m_Logger->EnterLog(LogLevel::Error, "video file is closed, cannot capture frame");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }

    // pace to the configured FPS
    int delayMs = (m_Fps > 0) ? (1000 / m_Fps) : 33;
    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

    // read() grabs and decodes one frame by itself: calling grab() first as well would throw every other frame away
    cv::Mat mat;
    bool read = m_Capture.read(mat) && !mat.empty();
    if (!read) {
        // the end of the file: play it again from the start, like a camera that never stops producing frames
        m_Capture.set(cv::CAP_PROP_POS_FRAMES, 0);
        read = m_Capture.read(mat) && !mat.empty();
    }

    if (read) {
        m_LoggedReadFailure = false;
        SetLatestResult(SourceResult(std::nullopt, mat));
    } else if (!m_LoggedReadFailure) {
        // not even the first frame can be read: report it once and wait, instead of an error line per frame interval
        m_Logger->EnterLog(LogLevel::Error, "Failed to read a frame from the video file, not even after rewinding it");
        m_LoggedReadFailure = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
