#include "ImageFileSource.h"
#include <opencv2/opencv.hpp>

ImageFileFrameSource::ImageFileFrameSource(std::string filePath, std::shared_ptr<Logger> logger, std::string m_ID) : ISource(logger, m_ID) {
    cv::Mat img = cv::imread(filePath);
    if (!img.empty()) {
        mat = img;
        // a static image changes once, at load, and there is no capture thread (m_DoNotLoadCaptureThread) to call
        // SetLatestResult() later, so it happens here or bound sinks never see a result.
        SetLatestResult(SourceResult(std::nullopt, mat));
    } else {
        mat = cv::Mat();
        logger->EnterLog(LogLevel::Error, "Failed to load image from file: " + filePath);
    }
    m_DoNotLoadCaptureThread = true;
}

void ImageFileFrameSource::CaptureFrame()
{
    // No operation needed as the image is already loaded
}