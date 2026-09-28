#pragma once
#include "ISource.h"
#include "ISink.h"
#include "IStereoRoleReceiver.h"
#include "CameraCalibrationResult.h"
#include "StereoCalibrationResult.h"
#include "CalibrationBoardType.h"
#include "StereoPairer.h"

#include <opencv2/calib3d.hpp>
#include <mutex>
#include <optional>

// Checkerboard only: a ChArUco board can detect different corner subsets per eye, which cv::stereoCalibrate cannot
// consume. BOARD_CHARUCO is accepted for API symmetry but throws.
struct StereoCalibrationBoardConfig {
	CalibrationBoardType type = BOARD_CHECKERBOARD;
	int rows = 6;
	int cols = 9;
	float squareSizeMeters = 0.025f;
};

// ISource+ISink, maxSources=2 (left/right). Same detection logic as CameraCalibrator (findChessboardCorners +
// cornerSubPix), applied per eye per paired frame.
class StereoCalibrator : public ISink, public ISource, public IStereoRoleReceiver
{
public:
	StereoCalibrator(std::shared_ptr<Logger> logger, std::string id,
		StereoCalibrationBoardConfig boardConfig = StereoCalibrationBoardConfig(),
		int64_t maxSkewUs = 33000 /* ~1 frame at 30fps */);

	// IStereoRoleReceiver
	void SetStereoRoles(const std::string& leftSourceId, const std::string& rightSourceId) override;

	// optional: seed per-eye intrinsics from two CameraCalibrator nodes so RunCalibration() uses cv::CALIB_FIX_INTRINSIC;
	// if unset, intrinsics and extrinsics are solved together from the stereo snapshots
	void SetPriorIntrinsics(const CameraCalibrationResult& left, const CameraCalibrationResult& right);

	// saves the latest matched (both eyes found, within skew) checkerboard detection; returns false if none is
	// available (see GetLastPairStatusJson())
	bool SaveStereoDetection();
	int GetPairCount() const;
	bool RemovePair(int index);
	void ClearPairs();

	// one eye's corner points for one saved pair, flattened as [x0,y0,x1,y1,...]; empty if index is out of range
	// or eye isn't "left"/"right"
	std::vector<double> GetPairCorners(int index, const std::string& eye) const;
	int GetFrameWidth() const;
	int GetFrameHeight() const;

	// runs cv::stereoCalibrate + cv::stereoRectify over every saved pair; throws if fewer than 8 pairs are saved
	StereoCalibrationResult RunCalibration();
	StereoCalibrationResult GetCalibrationResult() const;

private:
	void Process(const std::vector<SourceResult>& results) override;
	bool DetectCheckerboard(const cv::Mat& gray, std::vector<cv::Point2f>& corners, std::vector<cv::Point3f>& objectPoints);

	std::shared_ptr<Logger> m_Logger;
	StereoCalibrationBoardConfig m_BoardConfig;
	int64_t m_MaxSkewUs;

	std::string m_LeftSourceId, m_RightSourceId;

	// constructed once SetStereoRoles() supplies real source ids (see StereoPairer.h)
	std::optional<StereoPairer> m_Pairer;

	mutable std::mutex m_DetectionMutex;
	bool m_LastPairFound = false;
	int64_t m_LastSkewUs = -1;
	bool m_LastFoundLeft = false, m_LastFoundRight = false;
	std::vector<cv::Point2f> m_LastLeftCorners, m_LastRightCorners;
	std::vector<cv::Point3f> m_LastObjectPoints;
	cv::Size m_LastFrameSize;

	std::vector<std::vector<cv::Point3f>> m_ObjPoints;
	std::vector<std::vector<cv::Point2f>> m_LeftImgPoints, m_RightImgPoints;
	cv::Size m_FrameSize;

	std::optional<CameraCalibrationResult> m_PriorLeft, m_PriorRight;
	std::optional<StereoCalibrationResult> m_LastResult;
};
