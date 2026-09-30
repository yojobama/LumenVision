#pragma once
#include "ISink.h"
#include "ISource.h"
#include "IApriltagBackend.h"
#include "AprilTagFieldLayout.h"
#include "CameraCalibrationResult.h"
#include "CoordinateFrames.h"
#include <apriltag/apriltag_pose.h>
#include <opencv2/opencv.hpp>
#include <opencv2/calib3d.hpp> // cv::undistortPoints - not pulled in by <opencv2/opencv.hpp> alone
#include <memory>
#include <mutex>

class Logger;

class ApriltagDetector : public ISink, public ISource
{
public:
	// See ApriltagTuning for what each knob's default means. Tuning is runtime-adjustable via
	// SinkManager.SetApriltagBackend. frameWidth/frameHeight are a hint for Vulkan and may be 0: the
	// GPU pipeline is sized to the first real frame (and rebuilt if the size changes).
	ApriltagDetector(std::shared_ptr<Logger> logger, std::string id, CameraCalibrationResult calibrationResult,
		double tagSize /* metres */,
		ApriltagBackendKind backendKind = APRILTAG_BACKEND_CPU,
		int frameWidth = 0, int frameHeight = 0,
		ApriltagTuning tuning = ApriltagTuning());
	~ApriltagDetector();

	// backend actually running; falls back to CPU if Vulkan is unavailable. Reports the request
	// until the first frame of a Vulkan-requested detector.
	std::string GetBackendName() const;
	ApriltagBackendKind GetBackendKind() const;

	// lets a caller rebuild an equivalent detector (e.g. to switch backend) without separate tracking
	double GetTagSize() const { return m_DetectionInfo.tagsize; }
	CameraCalibrationResult GetCalibration() const;

	// Pass-throughs to the active backend (or the requested tuning while a Vulkan backend is pending).
	int GetThreads() const;
	float GetQuadDecimate() const;
	bool GetQuadDecimateSupported() const;
	bool GetRefineEdges() const;
	RefineEdgesMode GetRefineMode() const;
	bool GetRefineModeSupported() const;
	// what was requested at construction
	ApriltagTuning GetRequestedTuning() const { return m_Tuning; }

	// Driver mode: when true, Process() skips detection and the NT4 publish and republishes the raw
	// camera frame.
	void SetDriverMode(bool enabled) { m_DriverMode = enabled; }
	bool GetDriverMode() const { return m_DriverMode; }

	// Loads a WPILib-format AprilTagFieldLayout JSON file, enabling multi-tag PnP: every visible tag with
	// a known field pose feeds one solvePnP, publishing a field-relative camera pose. False on load failure.
	bool LoadFieldLayout(const std::string& jsonPath) { return m_FieldLayout.LoadFromFile(jsonPath); }
	size_t GetFieldLayoutTagCount() const { return m_FieldLayout.size(); }

	// Enables the constrained solve: a robot flat on the floor, found from every visible tag with a known field pose, starting from
	// this robot pose (x, y in metres, yaw in radians) and with the camera at `robotToCamera` (WPILib axes). Needs a calibration and a field
	// layout; its result rides in the envelope as "constrained". Call again each loop to keep the seed current.
	void SetConstrainedSeed(double x, double y, double yawRadians, const frames::Pose3& robotToCamera);

	// The multi-tag PnP solve, public static so it can be unit-tested. Returns a null json if
	// tagCount < 2 or solvePnP fails; see ApriltagDetector.cpp for the field-to-camera convention.
	static nlohmann::json SolveMultiTagPnP(
		const std::vector<cv::Point3d>& objectPoints, const std::vector<cv::Point2d>& imagePoints,
		const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs, int tagCount);
private:
	void Process(const std::vector<SourceResult>& results) override;

	// Calibration intrinsics/distortion/resolution as a JSON sibling of "tags"/"multiTag"; null when
	// m_HasCalibration is false.
	nlohmann::json BuildCalibrationJson() const;

	// Builds the backend for the requested kind at the given frame size; Vulkan falls back to CPU
	// on any failure. Caller must hold m_BackendMutex.
	void BuildBackendLocked(int frameWidth, int frameHeight);

	// m_Backend is swapped by Process() (first frame / size change) while API threads read it through
	// the getters above, so those take the lock. Process() is the only writer and needn't lock.
	mutable std::mutex m_BackendMutex;
	std::unique_ptr<IApriltagBackend> m_Backend; // null only while a Vulkan build is pending
	ApriltagBackendKind m_RequestedBackendKind = APRILTAG_BACKEND_CPU;
	ApriltagBackendKind m_ActiveBackendKind = APRILTAG_BACKEND_CPU;
	ApriltagTuning m_Tuning;
	// set once Vulkan failed to build - so a missing GPU is logged once, not retried every frame
	bool m_VulkanUnavailable = false;

	std::shared_ptr<Logger> m_Logger;
	apriltag_detection_info_t m_DetectionInfo;
	// the CameraCalibrationResult as constructed, kept verbatim (rms/imageWidth/imageHeight are lost in
	// m_CameraMatrix/m_DistCoeffs) so GetCalibration() can return it unchanged
	CameraCalibrationResult m_OriginalCalibration;

	// present only when the calibration had distortion coefficients; otherwise pose estimation runs on
	// the raw corners
	bool m_HasDistortion = false;
	cv::Mat m_CameraMatrix;
	cv::Mat m_DistCoeffs;

	// false when constructed without a real calibration (fx/fy left at 0.0). estimate_tag_pose returns
	// invalid pointers for fx=fy=0, so pose estimation is skipped entirely without valid intrinsics.
	bool m_HasCalibration = false;

	bool m_DriverMode = false;

	AprilTagFieldLayout m_FieldLayout;

	std::mutex m_ConstrainedMutex;
	bool m_HasConstrainedSeed = false;
	double m_ConstrainedSeedX = 0.0, m_ConstrainedSeedY = 0.0, m_ConstrainedSeedYaw = 0.0;
	frames::Pose3 m_RobotToCamera;
};
