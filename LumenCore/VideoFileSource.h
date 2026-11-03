#pragma once
#include "ISource.h"
#include <string>
#include "Logger.h"

namespace cv {
	class VideoCapture;
}

class VideoFileFrameSource : public ISource
{
public:
	VideoFileFrameSource(std::shared_ptr<Logger> logger, std::string filePath, int fps, std::string m_ID);
private:
	void CaptureFrame() override;
	cv::VideoCapture m_Capture;
	int m_Fps;
	bool m_LoggedReadFailure = false; // a file that cannot be read at all is reported once, not every frame
};

