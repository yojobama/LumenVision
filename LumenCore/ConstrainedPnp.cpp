#include "ConstrainedPnp.h"
#include <opencv2/calib3d.hpp>
#include <cmath>

namespace {

using Vec3 = cv::Vec3d;

// maps WPILib-camera-frame vectors to OpenCV-camera-frame vectors (see CoordinateFrames.cpp)
const cv::Matx33d WpilibCameraToOpenCv(0, -1, 0, 0, 0, -1, 1, 0, 0);

cv::Matx33d Matx(const std::array<double, 9>& R) {
	return cv::Matx33d(R[0], R[1], R[2], R[3], R[4], R[5], R[6], R[7], R[8]);
}

// pixel residuals (projected - observed, x then y per point) for the robot pose (x, y, yaw)
bool Residuals(const ConstrainedPnpInput& input, const cv::Matx33d& cameraInRobotR, const Vec3& cameraInRobotT,
	double x, double y, double yaw, std::vector<double>& out)
{
	const double c = std::cos(yaw), s = std::sin(yaw);
	const cv::Matx33d robotR(c, -s, 0, s, c, 0, 0, 0, 1);

	// camera pose in the field, WPILib axes, then the OpenCV-axes world-to-camera transform projectPoints wants
	const cv::Matx33d cameraInFieldR = robotR * cameraInRobotR;
	const Vec3 cameraInFieldT = Vec3(x, y, 0.0) + robotR * cameraInRobotT;
	const cv::Matx33d cameraInFieldOpenCv = cameraInFieldR * WpilibCameraToOpenCv.t();
	const cv::Matx33d worldToCamera = cameraInFieldOpenCv.t();
	const Vec3 tvec = -(worldToCamera * cameraInFieldT);

	cv::Mat rvec;
	cv::Rodrigues(worldToCamera, rvec);

	// points behind the camera have no meaningful projection; treat the pose as invalid
	for (const cv::Point3d& p : input.fieldPoints) {
		Vec3 inCamera = worldToCamera * Vec3(p.x, p.y, p.z) + tvec;
		if (inCamera[2] <= 1e-6) return false;
	}

	std::vector<cv::Point2d> projected;
	cv::projectPoints(input.fieldPoints, rvec, cv::Mat(tvec), input.cameraMatrix, input.distCoeffs, projected);
	out.resize(projected.size() * 2);
	for (size_t i = 0; i < projected.size(); i++) {
		out[i * 2] = projected[i].x - input.imagePoints[i].x;
		out[i * 2 + 1] = projected[i].y - input.imagePoints[i].y;
	}
	return true;
}

double SumSquares(const std::vector<double>& r) {
	double sum = 0.0;
	for (double v : r) sum += v * v;
	return sum;
}

}

ConstrainedPnpResult SolveConstrainedPnp(const ConstrainedPnpInput& input)
{
	ConstrainedPnpResult result;
	if (input.fieldPoints.empty() || input.fieldPoints.size() != input.imagePoints.size()) return result;

	const cv::Matx33d cameraInRobotR = Matx(input.robotToCamera.R);
	const Vec3 cameraInRobotT(input.robotToCamera.t[0], input.robotToCamera.t[1], input.robotToCamera.t[2]);

	double params[3] = { input.seedX, input.seedY, input.seedYaw };
	std::vector<double> residual;
	if (!Residuals(input, cameraInRobotR, cameraInRobotT, params[0], params[1], params[2], residual)) return result;
	double cost = SumSquares(residual);

	double lambda = 1e-3;
	const int maxIterations = 60;
	int iteration = 0;
	for (; iteration < maxIterations; iteration++) {
		// numeric Jacobian (central differences) of the residual vector with respect to (x, y, yaw)
		const size_t m = residual.size();
		cv::Mat J(static_cast<int>(m), 3, CV_64F);
		bool jacobianOk = true;
		for (int k = 0; k < 3 && jacobianOk; k++) {
			const double step = 1e-6; // metres for x and y, radians for yaw
			double plus[3] = { params[0], params[1], params[2] };
			double minus[3] = { params[0], params[1], params[2] };
			plus[k] += step;
			minus[k] -= step;
			std::vector<double> rp, rm;
			if (!Residuals(input, cameraInRobotR, cameraInRobotT, plus[0], plus[1], plus[2], rp) ||
				!Residuals(input, cameraInRobotR, cameraInRobotT, minus[0], minus[1], minus[2], rm)) {
				jacobianOk = false;
				break;
			}
			for (size_t i = 0; i < m; i++) J.at<double>(static_cast<int>(i), k) = (rp[i] - rm[i]) / (2.0 * step);
		}
		if (!jacobianOk) break;

		cv::Mat r(static_cast<int>(m), 1, CV_64F);
		for (size_t i = 0; i < m; i++) r.at<double>(static_cast<int>(i), 0) = residual[i];
		cv::Mat JtJ = J.t() * J;
		cv::Mat Jtr = J.t() * r;

		bool improved = false;
		for (int attempt = 0; attempt < 12; attempt++) {
			cv::Mat damped = JtJ.clone();
			for (int d = 0; d < 3; d++) damped.at<double>(d, d) += lambda * std::max(JtJ.at<double>(d, d), 1e-9);
			cv::Mat delta;
			if (!cv::solve(damped, -Jtr, delta, cv::DECOMP_CHOLESKY)) {
				lambda *= 10.0;
				continue;
			}
			double candidate[3] = { params[0] + delta.at<double>(0), params[1] + delta.at<double>(1), params[2] + delta.at<double>(2) };
			std::vector<double> candidateResidual;
			if (Residuals(input, cameraInRobotR, cameraInRobotT, candidate[0], candidate[1], candidate[2], candidateResidual)) {
				double candidateCost = SumSquares(candidateResidual);
				if (candidateCost < cost) {
					const double change = cost - candidateCost;
					params[0] = candidate[0];
					params[1] = candidate[1];
					params[2] = candidate[2];
					residual = std::move(candidateResidual);
					cost = candidateCost;
					lambda = std::max(lambda / 10.0, 1e-12);
					improved = true;
					if (change < 1e-12 * std::max(cost, 1.0) || cv::norm(delta) < 1e-10) iteration = maxIterations; // converged
					break;
				}
			}
			lambda *= 10.0;
		}
		if (!improved) break; // no step lowers the cost: at a minimum
	}

	result.ok = true;
	result.x = params[0];
	result.y = params[1];
	result.yaw = std::atan2(std::sin(params[2]), std::cos(params[2]));
	result.reprojErrPixels = std::sqrt(cost / static_cast<double>(input.fieldPoints.size()));
	result.iterations = iteration;
	return result;
}
