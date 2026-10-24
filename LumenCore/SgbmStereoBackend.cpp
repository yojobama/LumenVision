#include "SgbmStereoBackend.h"

SgbmStereoBackend::SgbmStereoBackend(int blockW, int blockH, int minDisparity, int numDisparities, int downscale)
	: m_BlockW(blockW), m_BlockH(blockH), m_MinDisparity(minDisparity), m_NumDisparities(numDisparities)
{
	// numDisparities is rounded up to a multiple of 16 rather than rejected
	if (m_NumDisparities % 16 != 0) m_NumDisparities += 16 - (m_NumDisparities % 16);
	if (m_NumDisparities < 16) m_NumDisparities = 16;

	m_Downscale = (downscale == 2 && blockW % 2 == 0 && blockH % 2 == 0) ? 2 : 1;
	// the search range in the matched image's own pixels
	m_SearchMinDisparity = m_MinDisparity / m_Downscale;
	int searchNumDisparities = (m_NumDisparities + m_Downscale - 1) / m_Downscale;
	if (searchNumDisparities % 16 != 0) searchNumDisparities += 16 - (searchNumDisparities % 16);
	if (searchNumDisparities < 16) searchNumDisparities = 16;

	// blockSize must be odd for SGBM; block_w/h here describe the OUTPUT aggregation grid, not
	// SGBM's own matching window, so a small fixed window is used regardless of block_w/h.
	int sadWindowSize = 5;
	m_Sgbm = cv::StereoSGBM::create(
		m_SearchMinDisparity, searchNumDisparities, sadWindowSize,
		8 * 1 * sadWindowSize * sadWindowSize,
		32 * 1 * sadWindowSize * sadWindowSize,
		1, 63, 10, 100, 32,
		cv::StereoSGBM::MODE_SGBM_3WAY);
}

bool SgbmStereoBackend::Compute(const cv::Mat& rectLeft, const cv::Mat& rectRight,
	std::vector<float>& disparityOut, int& cols, int& rows)
{
	if (rectLeft.empty() || rectRight.empty() || rectLeft.size() != rectRight.size()) return false;

	cv::Mat rawDisparity; // CV_16S, fixed-point: real disparity = raw / 16.0, in the matched image's pixels
	if (m_Downscale == 2) {
		cv::Mat halfLeft, halfRight;
		cv::resize(rectLeft, halfLeft, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
		cv::resize(rectRight, halfRight, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
		m_Sgbm->compute(halfLeft, halfRight, rawDisparity);
	} else {
		m_Sgbm->compute(rectLeft, rectRight, rawDisparity);
	}

	cols = rectLeft.cols / m_BlockW;
	rows = rectLeft.rows / m_BlockH;
	// a block covers blockW x blockH full-resolution pixels, so blockW/2 x blockH/2 of the half-resolution disparity map, whose
	// values are half of the full-resolution ones
	const int matchedBlockW = m_BlockW / m_Downscale, matchedBlockH = m_BlockH / m_Downscale;
	disparityOut.assign((size_t)cols * rows, STEREO_DISPARITY_INVALID);

	// OpenCV marks an unmatched pixel with (minDisparity - 1) * 16 (see StereoSGBM docs); a
	// pixel at or below that fixed-point value never contributes to its block's average.
	const int invalidRawCeiling = (m_SearchMinDisparity - 1) * 16;

	std::vector<int> validCount((size_t)cols * rows, 0);
	std::vector<double> sum((size_t)cols * rows, 0.0);

	for (int by = 0; by < rows; ++by) {
		for (int bx = 0; bx < cols; ++bx) {
			int y0 = by * matchedBlockH, y1 = std::min(y0 + matchedBlockH, rawDisparity.rows);
			int x0 = bx * matchedBlockW, x1 = std::min(x0 + matchedBlockW, rawDisparity.cols);
			int idx = by * cols + bx;
			for (int y = y0; y < y1; ++y) {
				const int16_t* row = rawDisparity.ptr<int16_t>(y);
				for (int x = x0; x < x1; ++x) {
					int raw = row[x];
					if (raw <= invalidRawCeiling) continue;
					sum[idx] += raw / 16.0;
					validCount[idx]++;
				}
			}
		}
	}

	for (size_t idx = 0; idx < sum.size(); ++idx) {
		if (validCount[idx] > 0) disparityOut[idx] = (float)(sum[idx] / validCount[idx] * m_Downscale);
	}

	return true;
}
