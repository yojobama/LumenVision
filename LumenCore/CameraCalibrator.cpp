#include "CameraCalibrator.h"
#include <stdexcept>

namespace {
	// legacy default for the no-config constructor
	const cv::Size LEGACY_CHECKERBOARD_PATTERN_SIZE(6, 9);
}

CameraCalibrator::CameraCalibrator(std::shared_ptr<Logger> logger, std::string id, CalibrationBoardConfig boardConfig)
	: ISink(logger, 1, false, true, id), ISource(logger, id), m_Logger(logger), m_BoardConfig(boardConfig)
{
	m_DoNotLoadCaptureThread = true;

	if (m_BoardConfig.type == BOARD_CHARUCO) {
		cv::aruco::Dictionary dictionary = cv::aruco::getPredefinedDictionary(m_BoardConfig.arucoDictionaryId);
		m_CharucoBoard.emplace(
			cv::Size(m_BoardConfig.cols, m_BoardConfig.rows),
			m_BoardConfig.squareSizeMeters, m_BoardConfig.markerSizeMeters, dictionary);
		m_CharucoDetector.emplace(*m_CharucoBoard);
	}
}

CameraCalibrationResult CameraCalibrator::RunCalibration()
{
	if (m_ObjPoints.size() < 4) {
		throw std::runtime_error("CameraCalibrator::RunCalibration: need at least 4 saved snapshots, have " + std::to_string(m_ObjPoints.size()));
	}

	cv::Mat cameraMatrix = cv::Mat(3, 3, CV_64F);
	cv::Mat distCoeffsMat = cv::Mat(8, 1, CV_64F);
	std::vector<cv::Mat> rvecs, tvecs;

	double rms = cv::calibrateCamera(m_ObjPoints, m_ImgPoints, frameSize, cameraMatrix, distCoeffsMat, rvecs, tvecs);

	double fx = cameraMatrix.at<double>(0, 0);
	double fy = cameraMatrix.at<double>(1, 1);
	double cx = cameraMatrix.at<double>(0, 2);
	double cy = cameraMatrix.at<double>(1, 2);

	std::vector<double> distCoeffs(distCoeffsMat.begin<double>(), distCoeffsMat.end<double>());

	m_LastResult = CameraCalibrationResult(fx, fy, cx, cy, rms, distCoeffs, frameSize.width, frameSize.height);
	if (m_Logger) m_Logger->EnterLog("CameraCalibrator: RunCalibration produced rms=" + std::to_string(rms) + " over " + std::to_string(m_ObjPoints.size()) + " snapshot(s)");
	return m_LastResult.value();
}

CameraCalibrationResult CameraCalibrator::GetCalibrationResult() const
{
	return m_LastResult.value_or(CameraCalibrationResult());
}

bool CameraCalibrator::SaveBoardDetection()
{
	std::lock_guard<std::mutex> lock(m_DetectionMutex);

	if (!m_LastPatternFound) {
		m_Logger->EnterLog(LogLevel::Warning, "CameraCalibrator: SaveBoardDetection called but no board was detected in the latest frame.");
		return false;
	}

	m_ObjPoints.push_back(m_LastObjectPoints);
	m_ImgPoints.push_back(m_LastCorners);
	frameSize = m_LastFrameSize;

	// the snapshot has been consumed, require a fresh detection before it can be saved again
	m_LastPatternFound = false;

	m_Logger->EnterLog("CameraCalibrator: board detection saved, total snapshots=" + std::to_string(m_ImgPoints.size()));
	return true;
}

int CameraCalibrator::GetSnapshotCount() const
{
	std::lock_guard<std::mutex> lock(m_DetectionMutex);
	return static_cast<int>(m_ImgPoints.size());
}

bool CameraCalibrator::RemoveSnapshot(int index)
{
	std::lock_guard<std::mutex> lock(m_DetectionMutex);
	if (index < 0 || static_cast<size_t>(index) >= m_ImgPoints.size()) return false;

	m_ObjPoints.erase(m_ObjPoints.begin() + index);
	m_ImgPoints.erase(m_ImgPoints.begin() + index);
	return true;
}

void CameraCalibrator::ClearSnapshots()
{
	std::lock_guard<std::mutex> lock(m_DetectionMutex);
	m_ObjPoints.clear();
	m_ImgPoints.clear();
}

std::vector<double> CameraCalibrator::GetSnapshotCorners(int index) const
{
	std::lock_guard<std::mutex> lock(m_DetectionMutex);
	if (index < 0 || static_cast<size_t>(index) >= m_ImgPoints.size()) return {};

	std::vector<double> flattened;
	flattened.reserve(m_ImgPoints[index].size() * 2);
	for (const cv::Point2f& corner : m_ImgPoints[index]) {
		flattened.push_back(corner.x);
		flattened.push_back(corner.y);
	}
	return flattened;
}

int CameraCalibrator::GetFrameWidth() const
{
	std::lock_guard<std::mutex> lock(m_DetectionMutex);
	return frameSize.width;
}

int CameraCalibrator::GetFrameHeight() const
{
	std::lock_guard<std::mutex> lock(m_DetectionMutex);
	return frameSize.height;
}

void CameraCalibrator::ProcessCheckerboard(const cv::Mat& gray, cv::Mat& displayFrame)
{
	cv::Size patternSize(m_BoardConfig.cols, m_BoardConfig.rows);

	std::vector<cv::Point2f> corners;
	bool patternFound = cv::findChessboardCorners(gray, patternSize, corners,
		cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_FAST_CHECK);

	if (patternFound) {
		cv::cornerSubPix(gray, corners, cv::Size(11, 11), cv::Size(-1, -1),
			cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));

		cv::drawChessboardCorners(displayFrame, patternSize, corners, patternFound);
		cv::putText(displayFrame, "BOARD DETECTED", cv::Point(20, 40),
			cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);

		std::vector<cv::Point3f> objp;
		for (int i = 0; i < m_BoardConfig.rows; i++) {
			for (int j = 0; j < m_BoardConfig.cols; j++) {
				objp.push_back(cv::Point3f(j * m_BoardConfig.squareSizeMeters, i * m_BoardConfig.squareSizeMeters, 0));
			}
		}

		std::lock_guard<std::mutex> lock(m_DetectionMutex);
		m_LastPatternFound = true;
		m_LastCorners = corners;
		m_LastObjectPoints = objp;
		m_LastFrameSize = gray.size();
	} else {
		cv::putText(displayFrame, "Searching for board...", cv::Point(20, 40),
			cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
		std::lock_guard<std::mutex> lock(m_DetectionMutex);
		m_LastPatternFound = false;
	}
}

void CameraCalibrator::ProcessCharuco(const cv::Mat& gray, cv::Mat& displayFrame)
{
	std::vector<cv::Point2f> charucoCorners;
	std::vector<int> charucoIds;
	m_CharucoDetector->detectBoard(gray, charucoCorners, charucoIds);

	// require a spread of corners: a few stray ones give a near-degenerate snapshot that
	// cv::calibrateCamera would accept, poisoning the result
	bool patternFound = charucoCorners.size() >= 6 && !m_CharucoBoard->checkCharucoCornersCollinear(charucoIds);

	if (patternFound) {
		cv::aruco::drawDetectedCornersCharuco(displayFrame, charucoCorners, charucoIds, cv::Scalar(0, 255, 0));
		cv::putText(displayFrame, "BOARD DETECTED (" + std::to_string(charucoCorners.size()) + " corners)", cv::Point(20, 40),
			cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);

		const std::vector<cv::Point3f>& allBoardCorners = m_CharucoBoard->getChessboardCorners();
		std::vector<cv::Point3f> objp;
		objp.reserve(charucoIds.size());
		for (int id : charucoIds) {
			objp.push_back(allBoardCorners[id]);
		}

		std::lock_guard<std::mutex> lock(m_DetectionMutex);
		m_LastPatternFound = true;
		m_LastCorners = charucoCorners;
		m_LastObjectPoints = objp;
		m_LastFrameSize = gray.size();
	} else {
		cv::putText(displayFrame, "Searching for board...", cv::Point(20, 40),
			cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
		std::lock_guard<std::mutex> lock(m_DetectionMutex);
		m_LastPatternFound = false;
	}
}

void CameraCalibrator::Process(const std::vector<SourceResult>& results)
{
	const Frame& frame = results[0].frame.value();

	if (frame.empty()) {
		m_Logger->EnterLog(LogLevel::Error, "CameraCalibrator: Blank frame grabbed.");
		return;
	}

	// Create a copy to draw overlays onto without corrupting raw capture data
	cv::Mat displayFrame = frame.AsBgr().clone();
	const cv::Mat& gray = frame.AsGray();

	if (m_BoardConfig.type == BOARD_CHARUCO) {
		ProcessCharuco(gray, displayFrame);
	} else {
		ProcessCheckerboard(gray, displayFrame);
	}

	std::string countText = "Saved Snapshots: " + std::to_string(GetSnapshotCount());
	cv::putText(displayFrame, countText, cv::Point(20, displayFrame.rows - 20),
		cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);

	SetLatestResult(SourceResult(std::nullopt, displayFrame));
}
