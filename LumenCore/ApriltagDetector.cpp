#include "ApriltagDetector.h"
#include "ApriltagDetection.h"
#include "CameraCalibrationResult.h"
#include "CpuApriltagBackend.h"
#include "FramePool.h"
#ifdef LUMEN_WITH_VULKAN_APRILTAG
#include "VkApriltagBackend.h"
#endif

ApriltagDetector::ApriltagDetector(std::shared_ptr<Logger> logger, std::string id, CameraCalibrationResult cameraCalibrationResult,
	double tagSize, ApriltagBackendKind backendKind, int frameWidth, int frameHeight, ApriltagTuning tuning)
	// requireColor=false: Process() only calls AsGray() on its input, so it must not force a colour
	// decode upstream (see ISource::HasActiveColorFrameConsumer).
	: ISource(logger, id), ISink(logger, 1, false, true, id, false)
{
	if (logger) logger->EnterLog("ApriltagDetector constructed");

	m_Logger = logger;
	m_RequestedBackendKind = backendKind;
	m_Tuning = tuning;
	{
		std::lock_guard<std::mutex> lock(m_BackendMutex);
		// Vulkan with no known frame size yet: the first frame builds the backend (see Process).
		if (backendKind != APRILTAG_BACKEND_VULKAN || (frameWidth > 0 && frameHeight > 0))
			BuildBackendLocked(frameWidth, frameHeight);
		else
			m_ActiveBackendKind = APRILTAG_BACKEND_VULKAN; // pending - reported as the request
	}

	m_OriginalCalibration = cameraCalibrationResult;
	m_DetectionInfo.tagsize = tagSize;
	m_DetectionInfo.fx = cameraCalibrationResult.fx;
	m_DetectionInfo.fy = cameraCalibrationResult.fy;
	m_DetectionInfo.cx = cameraCalibrationResult.cx;
	m_DetectionInfo.cy = cameraCalibrationResult.cy;
	m_HasCalibration = cameraCalibrationResult.fx > 0.0 && cameraCalibrationResult.fy > 0.0;

	// m_CameraMatrix must exist whenever m_HasCalibration does; multi-tag PnP needs intrinsics even
	// when the fitted distortion is zero.
	if (m_HasCalibration) {
		m_CameraMatrix = (cv::Mat_<double>(3, 3) <<
			cameraCalibrationResult.fx, 0, cameraCalibrationResult.cx,
			0, cameraCalibrationResult.fy, cameraCalibrationResult.cy,
			0, 0, 1);
	}
	if (cameraCalibrationResult.HasDistortion()) {
		m_HasDistortion = true;
		m_DistCoeffs = cv::Mat(cameraCalibrationResult.distCoeffs, true /* copy */);
	} else {
		m_DistCoeffs = cv::Mat::zeros(5, 1, CV_64F);
	}

	m_Logger = logger;

	m_DoNotLoadCaptureThread = true;
}

ApriltagDetector::~ApriltagDetector() = default;

CameraCalibrationResult ApriltagDetector::GetCalibration() const
{
	return m_OriginalCalibration;
}

void ApriltagDetector::BuildBackendLocked(int frameWidth, int frameHeight)
{
	if (m_RequestedBackendKind == APRILTAG_BACKEND_VULKAN && !m_VulkanUnavailable) {
#ifdef LUMEN_WITH_VULKAN_APRILTAG
		try {
			auto vk = std::make_unique<VkApriltagBackend>(frameWidth, frameHeight, m_Tuning);
			if (m_Logger) {
				std::string msg = "Vulkan AprilTag backend built for " + std::to_string(frameWidth) + "x" +
					std::to_string(frameHeight) + ", decimation " + std::to_string(static_cast<int>(vk->GetQuadDecimate())) +
					", refineEdges " + (vk->GetRefineEdges() ? "on" : "off") +
					", refineMode " + std::to_string(static_cast<int>(vk->GetRefineMode()));
				if (m_Tuning.quadDecimate > 0.0f && vk->GetQuadDecimate() != m_Tuning.quadDecimate)
					msg += " (requested decimation " + std::to_string(m_Tuning.quadDecimate) +
						" isn't an integer that divides the frame size)";
				m_Logger->EnterLog(msg);
				std::string envOverride = VkApriltagBackend::RefineEnvOverride();
				if (!envOverride.empty())
					m_Logger->EnterLog(LogLevel::Warning, "APRILTAG_VK_REFINE=" + envOverride +
						" overrides the requested refine mode");
			}
			m_Backend = std::move(vk);
			m_ActiveBackendKind = APRILTAG_BACKEND_VULKAN;
			return;
		} catch (const std::exception& e) {
			// No usable Vulkan device or setup failed: fall back to CPU, without retrying per frame.
			if (m_Logger) m_Logger->EnterLog(LogLevel::Warning,
				std::string("Vulkan AprilTag backend unavailable (") + e.what() + "), falling back to CPU");
			m_VulkanUnavailable = true;
		}
#else
		if (m_Logger) m_Logger->EnterLog(LogLevel::Warning,
			"Vulkan AprilTag backend requested but LUMEN_WITH_VULKAN_APRILTAG was not compiled in, falling back to CPU");
		m_VulkanUnavailable = true;
#endif
	}
	m_Backend = std::make_unique<CpuApriltagBackend>(m_Tuning);
	m_ActiveBackendKind = APRILTAG_BACKEND_CPU;
}

std::string ApriltagDetector::GetBackendName() const
{
	std::lock_guard<std::mutex> lock(m_BackendMutex);
	return m_Backend ? m_Backend->Name() : "Vulkan (vkapriltag) - starting on first frame";
}

ApriltagBackendKind ApriltagDetector::GetBackendKind() const
{
	std::lock_guard<std::mutex> lock(m_BackendMutex);
	return m_ActiveBackendKind;
}

int ApriltagDetector::GetThreads() const
{
	std::lock_guard<std::mutex> lock(m_BackendMutex);
	return m_Backend ? m_Backend->GetThreads() : m_Tuning.nthreads;
}

float ApriltagDetector::GetQuadDecimate() const
{
	std::lock_guard<std::mutex> lock(m_BackendMutex);
	return m_Backend ? m_Backend->GetQuadDecimate() : m_Tuning.quadDecimate;
}

bool ApriltagDetector::GetQuadDecimateSupported() const
{
	std::lock_guard<std::mutex> lock(m_BackendMutex);
	return m_Backend ? m_Backend->GetQuadDecimateSupported() : true;
}

bool ApriltagDetector::GetRefineEdges() const
{
	std::lock_guard<std::mutex> lock(m_BackendMutex);
	return m_Backend ? m_Backend->GetRefineEdges() : m_Tuning.refineEdges;
}

RefineEdgesMode ApriltagDetector::GetRefineMode() const
{
	std::lock_guard<std::mutex> lock(m_BackendMutex);
	return m_Backend ? m_Backend->GetRefineMode() : m_Tuning.refineMode;
}

bool ApriltagDetector::GetRefineModeSupported() const
{
	std::lock_guard<std::mutex> lock(m_BackendMutex);
	return m_Backend ? m_Backend->GetRefineModeSupported() : true;
}

nlohmann::json ApriltagDetector::SolveMultiTagPnP(
	const std::vector<cv::Point3d>& objectPoints, const std::vector<cv::Point2d>& imagePoints,
	const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs, int tagCount)
{
	// requires at least 2 tags; a single tag's corners would only reproduce the per-tag estimate.
	if (tagCount < 2) return nullptr;

	cv::Mat rvec, tvec;
	bool solved = cv::solvePnP(objectPoints, imagePoints, cameraMatrix, distCoeffs, rvec, tvec);
	if (!solved) return nullptr;

	cv::Mat fieldToCameraRotation;
	cv::Rodrigues(rvec, fieldToCameraRotation);

	// solvePnP's (rvec, tvec) map field points into the camera frame (p_camera = R*p_field + t);
	// the camera pose in field frame is the inverse.
	cv::Mat cameraRotationInField = fieldToCameraRotation.t();
	cv::Mat cameraTranslationInField = -cameraRotationInField * tvec;

	std::vector<cv::Point2d> reprojected;
	cv::projectPoints(objectPoints, rvec, tvec, cameraMatrix, distCoeffs, reprojected);
	double squaredErrorSum = 0.0;
	for (size_t p = 0; p < reprojected.size(); p++) {
		double dx = reprojected[p].x - imagePoints[p].x;
		double dy = reprojected[p].y - imagePoints[p].y;
		squaredErrorSum += dx * dx + dy * dy;
	}
	double reprojectionErrorPixels = std::sqrt(squaredErrorSum / reprojected.size());

	return {
		{"x", cameraTranslationInField.at<double>(0)},
		{"y", cameraTranslationInField.at<double>(1)},
		{"z", cameraTranslationInField.at<double>(2)},
		{"R", {
			{cameraRotationInField.at<double>(0,0), cameraRotationInField.at<double>(0,1), cameraRotationInField.at<double>(0,2)},
			{cameraRotationInField.at<double>(1,0), cameraRotationInField.at<double>(1,1), cameraRotationInField.at<double>(1,2)},
			{cameraRotationInField.at<double>(2,0), cameraRotationInField.at<double>(2,1), cameraRotationInField.at<double>(2,2)}
		}},
		{"tagCount", tagCount},
		{"reprojErrPixels", reprojectionErrorPixels}
	};
}

nlohmann::json ApriltagDetector::BuildCalibrationJson() const
{
	if (!m_HasCalibration) return nullptr;
	return {
		{"fx", m_OriginalCalibration.fx},
		{"fy", m_OriginalCalibration.fy},
		{"cx", m_OriginalCalibration.cx},
		{"cy", m_OriginalCalibration.cy},
		{"distCoeffs", m_OriginalCalibration.distCoeffs},
		{"imageWidth", m_OriginalCalibration.imageWidth},
		{"imageHeight", m_OriginalCalibration.imageHeight},
	};
}

void ApriltagDetector::Process(const std::vector<SourceResult>& results)
{
	for (const SourceResult& result : results)
	{
		if (result.frame.has_value())
		{
			if (m_DriverMode) {
				// Driver mode: republish the raw frame in an empty tags/multiTag envelope, skipping detection.
				// AsBgrFrame() keeps pool ownership; a bare cv::Mat would risk use-after-recycle.
				SetLatestResult(SourceResult(nlohmann::json{{"tags", nlohmann::json::array()}, {"multiTag", nullptr}, {"calibration", BuildCalibrationJson()}}, result.frame->AsBgrFrame(), result.captureTimeUs));
				continue;
			}

			// AsGray() is free for GRAY8/NV12 frames (no conversion).
			const cv::Mat& gray = result.frame->AsGray();

			// Vulkan is sized to the frame: build on the first frame, rebuild if the size changes.
			if (m_RequestedBackendKind == APRILTAG_BACKEND_VULKAN && !m_VulkanUnavailable) {
#ifdef LUMEN_WITH_VULKAN_APRILTAG
				auto* vk = dynamic_cast<VkApriltagBackend*>(m_Backend.get());
				bool needsBuild = !m_Backend ||
					(vk && (vk->GetFrameWidth() != gray.cols || vk->GetFrameHeight() != gray.rows));
#else
				bool needsBuild = !m_Backend;
#endif
				if (needsBuild) {
					std::lock_guard<std::mutex> lock(m_BackendMutex);
					BuildBackendLocked(gray.cols, gray.rows);
				}
			}

			// Debug level: this runs every frame and Info would pay a mutex lock plus a file write.
			m_Logger->EnterLog(LogLevel::Debug, "detecting apriltags using backend=" + m_Backend->Name());
			zarray_t* detections = m_Backend->Detect(gray);

			// Only draw the annotated frame when a consumer (e.g. a WebRTC preview) wants it.
			bool wantsFrame = HasActiveFrameConsumer();

			// Acquire()+copyTo() recycles a pooled buffer; carry `colourOwner` into SetLatestResult via
			// Frame's pool-owner constructor (see AsBgrFrame).
			std::shared_ptr<void> colourOwner;
			cv::Mat colouredFrame;
			if (wantsFrame) {
				const cv::Mat& sourceBgr = result.frame->AsBgr();
				colouredFrame = FramePool::Instance().Acquire(sourceBgr.rows, sourceBgr.cols, sourceBgr.type(), colourOwner);
				sourceBgr.copyTo(colouredFrame);
			}

			std::vector<nlohmann::json> jsonVector;

			// Multi-tag PnP: accumulates tags with a known field pose, solved jointly after the tag loop.
			std::vector<cv::Point3d> multiTagObjectPoints;
			std::vector<cv::Point2d> multiTagImagePoints;
			int multiTagCount = 0;

			for (int i = 0; i < zarray_size(detections); i++) {
				apriltag_detection_t* detection;
				zarray_get(detections, i, &detection);

				// estimate_tag_pose assumes a pinhole model, so run it on undistorted corners. The JSON and
				// overlay corners stay distorted, as they describe the actual image.
				nlohmann::json detectionJson = {
					{"id", detection->id},
					{"center", {detection->c[0], detection->c[1]}},
					{"corners", {
						{detection->p[0][0], detection->p[0][1]},
						{detection->p[1][0], detection->p[1][1]},
						{detection->p[2][0], detection->p[2][1]},
						{detection->p[3][0], detection->p[3][1]}
					}}
				};

				// Multi-tag PnP: field-frame 3D corners paired with the raw (distorted) pixels; solvePnP takes distCoeffs.
				// Local corner order matches detection->p[0..3], with +Z as the tag's outward normal (apriltag.c).
				AprilTagFieldPose fieldPose;
				if (m_HasCalibration && m_FieldLayout.TryGetTagPose(detection->id, fieldPose)) {
					double halfSize = m_DetectionInfo.tagsize / 2.0;
					cv::Vec3d localCorners[4] = {
						{-halfSize,  halfSize, 0},
						{ halfSize,  halfSize, 0},
						{ halfSize, -halfSize, 0},
						{-halfSize, -halfSize, 0},
					};
					cv::Vec3d fieldTranslation(fieldPose.translation.x, fieldPose.translation.y, fieldPose.translation.z);
					for (int corner = 0; corner < 4; corner++) {
						cv::Vec3d fieldPoint = fieldPose.rotation * localCorners[corner] + fieldTranslation;
						multiTagObjectPoints.emplace_back(fieldPoint[0], fieldPoint[1], fieldPoint[2]);
						multiTagImagePoints.emplace_back(detection->p[corner][0], detection->p[corner][1]);
					}
					multiTagCount++;
				}

				// estimate_tag_pose returns invalid pose pointers for degenerate intrinsics (fx=fy=0), which
				// corrupts the heap when freed, so skip pose estimation without valid intrinsics.
				if (m_HasCalibration) {
					apriltag_detection_t poseDetection = *detection;
					if (m_HasDistortion) {
						std::vector<cv::Point2d> distortedCorners = {
							{ detection->p[0][0], detection->p[0][1] },
							{ detection->p[1][0], detection->p[1][1] },
							{ detection->p[2][0], detection->p[2][1] },
							{ detection->p[3][0], detection->p[3][1] }
						};
						std::vector<cv::Point2d> undistortedCorners;
						// Passing m_CameraMatrix as the new matrix keeps the output in the input's pixel scale.
						cv::undistortPoints(distortedCorners, undistortedCorners, m_CameraMatrix, m_DistCoeffs, cv::noArray(), m_CameraMatrix);
						for (int corner = 0; corner < 4; corner++) {
							poseDetection.p[corner][0] = undistortedCorners[corner].x;
							poseDetection.p[corner][1] = undistortedCorners[corner].y;
						}
					}

					m_DetectionInfo.det = &poseDetection;
					apriltag_pose_t pose;
					double err = estimate_tag_pose(&m_DetectionInfo, &pose);

					// R is row-major 3x3 (pose.R->data[i*3+j]); published whole for WPILib's Rotation3d(Matrix).
					detectionJson["pose"] = {
						{"x", pose.t->data[0]},
						{"y", pose.t->data[1]},
						{"z", pose.t->data[2]},
						{"R", {
							{pose.R->data[0], pose.R->data[1], pose.R->data[2]},
							{pose.R->data[3], pose.R->data[4], pose.R->data[5]},
							{pose.R->data[6], pose.R->data[7], pose.R->data[8]}
						}}
					};

					// estimate_tag_pose allocates pose.R/pose.t; the caller must free them (apriltag/common/matd.h).
					matd_destroy(pose.R);
					matd_destroy(pose.t);
				}

				jsonVector.push_back(detectionJson);

				if (wantsFrame) {
					cv::line(colouredFrame, cv::Point(detection->p[0][0], detection->p[0][1]),
						cv::Point(detection->p[1][0], detection->p[1][1]),
						cv::Scalar(0, 0xff, 0), 2);
					cv::line(colouredFrame, cv::Point(detection->p[0][0], detection->p[0][1]),
						cv::Point(detection->p[3][0], detection->p[3][1]),
						cv::Scalar(0, 0, 0xff), 2);
					cv::line(colouredFrame, cv::Point(detection->p[1][0], detection->p[1][1]),
						cv::Point(detection->p[2][0], detection->p[2][1]),
						cv::Scalar(0xff, 0, 0), 2);
					cv::line(colouredFrame, cv::Point(detection->p[2][0], detection->p[2][1]),
						cv::Point(detection->p[3][0], detection->p[3][1]),
						cv::Scalar(0xff, 0, 0), 2);

					std::stringstream ss;
					ss << detection->id;
					std::string text = ss.str();
					int fontface = cv::FONT_HERSHEY_SCRIPT_SIMPLEX;
					double fontscale = 1.0;
					int baseline;
					cv::Size textsize = cv::getTextSize(text, fontface, fontscale, 2,
						&baseline);
					cv::putText(colouredFrame, text, cv::Point(detection->c[0] - textsize.width / 2,
						detection->c[1] + textsize.height / 2),
						fontface, fontscale, cv::Scalar(0xff, 0x99, 0), 2);
				}
			}

			m_Backend->ReleaseResult(detections);

			nlohmann::json multiTagJson = SolveMultiTagPnP(
				multiTagObjectPoints, multiTagImagePoints, m_CameraMatrix, m_DistCoeffs, multiTagCount);

			std::optional<Frame> outputFrame;
			if (wantsFrame) outputFrame = Frame(colouredFrame, FrameFormat::BGR24, colourOwner);

			SetLatestResult(SourceResult(nlohmann::json{{"tags", jsonVector}, {"multiTag", multiTagJson}, {"calibration", BuildCalibrationJson()}},
				outputFrame, result.captureTimeUs));
		}
	}
}
