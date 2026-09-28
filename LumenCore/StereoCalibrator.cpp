#include "StereoCalibrator.h"
#include <stdexcept>
#include <cmath>

namespace {
	std::vector<double> MatToVec(const cv::Mat& m)
	{
		cv::Mat c = m.isContinuous() ? m : m.clone();
		return std::vector<double>(c.begin<double>(), c.end<double>());
	}
}

StereoCalibrator::StereoCalibrator(std::shared_ptr<Logger> logger, std::string id,
	StereoCalibrationBoardConfig boardConfig, int64_t maxSkewUs)
	: ISink(logger, 2, false, true, id), ISource(logger, id),
	  m_Logger(logger), m_BoardConfig(boardConfig), m_MaxSkewUs(maxSkewUs)
{
	m_DoNotLoadCaptureThread = true;

	if (m_BoardConfig.type == BOARD_CHARUCO) {
		throw std::runtime_error(
			"StereoCalibrator: ChArUco is not supported yet - a ChArUco board can detect a "
			"different corner subset per eye, which needs an ID-intersection step this node "
			"doesn't implement. Use BOARD_CHECKERBOARD.");
	}
}

void StereoCalibrator::SetStereoRoles(const std::string& leftSourceId, const std::string& rightSourceId)
{
	m_LeftSourceId = leftSourceId;
	m_RightSourceId = rightSourceId;
	m_Pairer.emplace(leftSourceId, rightSourceId, m_MaxSkewUs);
}

void StereoCalibrator::SetPriorIntrinsics(const CameraCalibrationResult& left, const CameraCalibrationResult& right)
{
	m_PriorLeft = left;
	m_PriorRight = right;
}

bool StereoCalibrator::DetectCheckerboard(const cv::Mat& gray, std::vector<cv::Point2f>& corners,
	std::vector<cv::Point3f>& objectPoints)
{
	cv::Size patternSize(m_BoardConfig.cols, m_BoardConfig.rows);
	bool found = cv::findChessboardCorners(gray, patternSize, corners,
		cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_FAST_CHECK);
	if (!found) return false;

	cv::cornerSubPix(gray, corners, cv::Size(11, 11), cv::Size(-1, -1),
		cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));

	objectPoints.clear();
	for (int i = 0; i < m_BoardConfig.rows; i++) {
		for (int j = 0; j < m_BoardConfig.cols; j++) {
			objectPoints.push_back(cv::Point3f(j * m_BoardConfig.squareSizeMeters, i * m_BoardConfig.squareSizeMeters, 0));
		}
	}
	return true;
}

void StereoCalibrator::Process(const std::vector<SourceResult>& results)
{
	if (!m_Pairer.has_value()) return; // SetStereoRoles hasn't run yet

	StereoPairer::FeedResult feed = m_Pairer->Feed(results);
	if (feed.outcome == StereoPairer::Outcome::WaitingForEye) return;
	if (feed.outcome == StereoPairer::Outcome::DroppedForSkew) {
		std::lock_guard<std::mutex> lock(m_DetectionMutex);
		m_LastSkewUs = feed.skewUs;
		m_LastPairFound = false;
		return;
	}

	int64_t skewUs = feed.skewUs;
	SourceResult left = feed.pair->first, right = feed.pair->second;

	if (!left.frame.has_value() || !right.frame.has_value() ||
		left.frame->empty() || right.frame->empty()) {
		if (m_Logger) m_Logger->EnterLog(LogLevel::Error, "StereoCalibrator: blank frame in a paired stereo result.");
		return;
	}

	const cv::Mat& grayLeft = left.frame->AsGray();
	const cv::Mat& grayRight = right.frame->AsGray();

	std::vector<cv::Point2f> leftCorners, rightCorners;
	std::vector<cv::Point3f> leftObjPoints, rightObjPoints;
	bool foundLeft = DetectCheckerboard(grayLeft, leftCorners, leftObjPoints);
	bool foundRight = DetectCheckerboard(grayRight, rightCorners, rightObjPoints);

	{
		std::lock_guard<std::mutex> lock(m_DetectionMutex);
		m_LastSkewUs = skewUs;
		m_LastFoundLeft = foundLeft;
		m_LastFoundRight = foundRight;
		m_LastPairFound = foundLeft && foundRight;
		if (m_LastPairFound) {
			m_LastLeftCorners = leftCorners;
			m_LastRightCorners = rightCorners;
			m_LastObjectPoints = leftObjPoints; // identical grid for both eyes
			m_LastFrameSize = grayLeft.size();
		}
	}

	// side-by-side display: each eye annotated with its own detection state
	cv::Mat displayLeft = left.frame->AsBgr().clone();
	cv::Mat displayRight = right.frame->AsBgr().clone();
	cv::Size patternSize(m_BoardConfig.cols, m_BoardConfig.rows);
	if (foundLeft) cv::drawChessboardCorners(displayLeft, patternSize, leftCorners, true);
	if (foundRight) cv::drawChessboardCorners(displayRight, patternSize, rightCorners, true);

	cv::Mat sideBySide;
	cv::hconcat(displayLeft, displayRight, sideBySide);
	cv::putText(sideBySide, "Pairs saved: " + std::to_string(GetPairCount()) +
		"  skew: " + std::to_string(skewUs) + "us" +
		"  L:" + (foundLeft ? "OK" : "--") + " R:" + (foundRight ? "OK" : "--"),
		cv::Point(20, sideBySide.rows - 20), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);

	nlohmann::json json = {
		{"foundLeft", foundLeft}, {"foundRight", foundRight},
		{"pairFound", m_LastPairFound}, {"skewUs", skewUs}, {"pairCount", GetPairCount()},
	};

	SetLatestResult(SourceResult(json, sideBySide));
}

bool StereoCalibrator::SaveStereoDetection()
{
	std::lock_guard<std::mutex> lock(m_DetectionMutex);
	if (!m_LastPairFound) {
		if (m_Logger) m_Logger->EnterLog(LogLevel::Warning,
			"StereoCalibrator: SaveStereoDetection called with no matched both-eyes detection "
			"available (foundLeft=" + std::to_string(m_LastFoundLeft) +
			" foundRight=" + std::to_string(m_LastFoundRight) + " skewUs=" + std::to_string(m_LastSkewUs) + ")");
		return false;
	}

	m_ObjPoints.push_back(m_LastObjectPoints);
	m_LeftImgPoints.push_back(m_LastLeftCorners);
	m_RightImgPoints.push_back(m_LastRightCorners);
	m_FrameSize = m_LastFrameSize;
	m_LastPairFound = false; // require a fresh matched pair before this can be saved again

	if (m_Logger) m_Logger->EnterLog("StereoCalibrator: pair saved, total=" + std::to_string(m_ObjPoints.size()));
	return true;
}

int StereoCalibrator::GetPairCount() const
{
	std::lock_guard<std::mutex> lock(m_DetectionMutex);
	return (int)m_ObjPoints.size();
}

bool StereoCalibrator::RemovePair(int index)
{
	std::lock_guard<std::mutex> lock(m_DetectionMutex);
	if (index < 0 || (size_t)index >= m_ObjPoints.size()) return false;
	m_ObjPoints.erase(m_ObjPoints.begin() + index);
	m_LeftImgPoints.erase(m_LeftImgPoints.begin() + index);
	m_RightImgPoints.erase(m_RightImgPoints.begin() + index);
	return true;
}

void StereoCalibrator::ClearPairs()
{
	std::lock_guard<std::mutex> lock(m_DetectionMutex);
	m_ObjPoints.clear();
	m_LeftImgPoints.clear();
	m_RightImgPoints.clear();
}

std::vector<double> StereoCalibrator::GetPairCorners(int index, const std::string& eye) const
{
	std::lock_guard<std::mutex> lock(m_DetectionMutex);

	const std::vector<std::vector<cv::Point2f>>* points = nullptr;
	if (eye == "left") points = &m_LeftImgPoints;
	else if (eye == "right") points = &m_RightImgPoints;
	else return {};

	if (index < 0 || static_cast<size_t>(index) >= points->size()) return {};

	std::vector<double> flattened;
	flattened.reserve((*points)[index].size() * 2);
	for (const cv::Point2f& corner : (*points)[index]) {
		flattened.push_back(corner.x);
		flattened.push_back(corner.y);
	}
	return flattened;
}

int StereoCalibrator::GetFrameWidth() const
{
	std::lock_guard<std::mutex> lock(m_DetectionMutex);
	return m_FrameSize.width;
}

int StereoCalibrator::GetFrameHeight() const
{
	std::lock_guard<std::mutex> lock(m_DetectionMutex);
	return m_FrameSize.height;
}

StereoCalibrationResult StereoCalibrator::RunCalibration()
{
	// stereo extrinsics have more DOF than a single-eye calibration, so too few views is actively misleading
	if (m_ObjPoints.size() < 8) {
		throw std::runtime_error("StereoCalibrator::RunCalibration: need at least 8 saved pairs, have " + std::to_string(m_ObjPoints.size()));
	}

	cv::Mat K1, D1, K2, D2;
	if (m_PriorLeft.has_value() && m_PriorRight.has_value()) {
		K1 = cv::Mat::eye(3, 3, CV_64F);
		K1.at<double>(0, 0) = m_PriorLeft->fx; K1.at<double>(1, 1) = m_PriorLeft->fy;
		K1.at<double>(0, 2) = m_PriorLeft->cx; K1.at<double>(1, 2) = m_PriorLeft->cy;
		D1 = cv::Mat(m_PriorLeft->distCoeffs, true).reshape(1, (int)m_PriorLeft->distCoeffs.size());
		K2 = cv::Mat::eye(3, 3, CV_64F);
		K2.at<double>(0, 0) = m_PriorRight->fx; K2.at<double>(1, 1) = m_PriorRight->fy;
		K2.at<double>(0, 2) = m_PriorRight->cx; K2.at<double>(1, 2) = m_PriorRight->cy;
		D2 = cv::Mat(m_PriorRight->distCoeffs, true).reshape(1, (int)m_PriorRight->distCoeffs.size());
	} else {
		// no prior intrinsics supplied: solve per-eye from the stereo snapshots first, then use CALIB_FIX_INTRINSIC as usual
		std::vector<cv::Mat> rvecs, tvecs;
		K1 = cv::Mat::eye(3, 3, CV_64F);
		D1 = cv::Mat::zeros(8, 1, CV_64F);
		cv::calibrateCamera(m_ObjPoints, m_LeftImgPoints, m_FrameSize, K1, D1, rvecs, tvecs);
		K2 = cv::Mat::eye(3, 3, CV_64F);
		D2 = cv::Mat::zeros(8, 1, CV_64F);
		cv::calibrateCamera(m_ObjPoints, m_RightImgPoints, m_FrameSize, K2, D2, rvecs, tvecs);
	}

	cv::Mat R, T, E, F;
	double stereoRms = cv::stereoCalibrate(
		m_ObjPoints, m_LeftImgPoints, m_RightImgPoints,
		K1, D1, K2, D2, m_FrameSize, R, T, E, F,
		cv::CALIB_FIX_INTRINSIC,
		cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 100, 1e-5));

	cv::Mat R1, R2, P1, P2, Q;
	cv::Rect validRoi1, validRoi2;
	cv::stereoRectify(K1, D1, K2, D2, m_FrameSize, R, T, R1, R2, P1, P2, Q,
		cv::CALIB_ZERO_DISPARITY, /*alpha=*/0, cv::Size(), &validRoi1, &validRoi2);

	StereoCalibrationResult result;
	result.left = CameraCalibrationResult(K1.at<double>(0, 0), K1.at<double>(1, 1), K1.at<double>(0, 2), K1.at<double>(1, 2),
		0.0, MatToVec(D1), m_FrameSize.width, m_FrameSize.height);
	result.right = CameraCalibrationResult(K2.at<double>(0, 0), K2.at<double>(1, 1), K2.at<double>(0, 2), K2.at<double>(1, 2),
		0.0, MatToVec(D2), m_FrameSize.width, m_FrameSize.height);
	result.R = MatToVec(R);
	result.T = MatToVec(T);
	result.E = MatToVec(E);
	result.F = MatToVec(F);
	result.R1 = MatToVec(R1);
	result.R2 = MatToVec(R2);
	result.P1 = MatToVec(P1);
	result.P2 = MatToVec(P2);
	result.Q = MatToVec(Q);
	result.stereoRms = stereoRms;
	result.baselineMeters = cv::norm(T);
	result.rectifiedFx = P1.at<double>(0, 0);
	result.rectifiedCx = P1.at<double>(0, 2);
	result.rectifiedCy = P1.at<double>(1, 2);
	result.imageWidth = m_FrameSize.width;
	result.imageHeight = m_FrameSize.height;
	result.roiLeftX = validRoi1.x; result.roiLeftY = validRoi1.y;
	result.roiLeftW = validRoi1.width; result.roiLeftH = validRoi1.height;
	result.roiRightX = validRoi2.x; result.roiRightY = validRoi2.y;
	result.roiRightW = validRoi2.width; result.roiRightH = validRoi2.height;

	// self-check: P2's baseline term and norm(T) must agree in sign and magnitude (cv::stereoRectify convention);
	// logged, not thrown, since epipolarRms is the real gate
	double p2Baseline = -P2.at<double>(0, 3) / P1.at<double>(0, 0);
	if (std::abs(p2Baseline - result.baselineMeters) > 1e-6 * std::max(1.0, result.baselineMeters)) {
		if (m_Logger) m_Logger->EnterLog(LogLevel::Warning,
			"StereoCalibrator: baseline self-check mismatch (norm(T)=" + std::to_string(result.baselineMeters) +
			" vs P2-derived=" + std::to_string(p2Baseline) + ")");
	}

	// epipolarRms: push every saved corner through the rectification maps and measure how far it lands off the epipolar line
	double sumAbsDy = 0.0;
	int64_t countDy = 0;
	for (size_t i = 0; i < m_LeftImgPoints.size(); i++) {
		std::vector<cv::Point2f> rectLeft, rectRight;
		cv::undistortPoints(m_LeftImgPoints[i], rectLeft, K1, D1, R1, P1);
		cv::undistortPoints(m_RightImgPoints[i], rectRight, K2, D2, R2, P2);
		for (size_t j = 0; j < rectLeft.size() && j < rectRight.size(); j++) {
			sumAbsDy += std::abs(rectLeft[j].y - rectRight[j].y);
			countDy++;
		}
	}
	result.epipolarRms = countDy > 0 ? (sumAbsDy / countDy) : -1.0;

	m_LastResult = result;
	if (m_Logger) m_Logger->EnterLog("StereoCalibrator: RunCalibration produced stereoRms=" + std::to_string(stereoRms) +
		" epipolarRms=" + std::to_string(result.epipolarRms) + " baseline=" + std::to_string(result.baselineMeters) +
		"m over " + std::to_string(m_ObjPoints.size()) + " pair(s)");
	return result;
}

StereoCalibrationResult StereoCalibrator::GetCalibrationResult() const
{
	return m_LastResult.value_or(StereoCalibrationResult());
}
