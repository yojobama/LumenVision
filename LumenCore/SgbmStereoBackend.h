#pragma once
#include "IStereoDepthBackend.h"

// cv::StereoSGBM, block-averaged onto the same cols x rows grid as the other stereo backends.
// Depends only on OpenCV, so it is always compiled in.
class SgbmStereoBackend : public IStereoDepthBackend {
public:
	// numDisparities must be a positive multiple of 16 (cv::StereoSGBM's own requirement).
	// downscale 2 matches at half resolution (about 8x less work; the result is averaged onto blocks of 16x16 or so anyway) and scales the
	// disparities back up; it needs an even block size and otherwise falls back to full resolution (1).
	SgbmStereoBackend(int blockW, int blockH, int minDisparity, int numDisparities, int downscale = 2);

	bool Compute(const cv::Mat& rectLeft, const cv::Mat& rectRight,
		std::vector<float>& disparityOut, int& cols, int& rows) override;

	std::string Name() const override { return "sgbm"; }
	int BlockW() const override { return m_BlockW; }
	int BlockH() const override { return m_BlockH; }

private:
	int m_BlockW, m_BlockH;
	int m_MinDisparity, m_NumDisparities; // as given, in full-resolution pixels
	int m_Downscale;
	int m_SearchMinDisparity;             // what SGBM actually searches: the above divided by m_Downscale
	cv::Ptr<cv::StereoSGBM> m_Sgbm;
};
