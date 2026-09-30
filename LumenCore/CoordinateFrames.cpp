#include "CoordinateFrames.h"
#include <cmath>

namespace frames {

namespace {
	using Mat3 = std::array<double, 9>;

	Mat3 Multiply(const Mat3& a, const Mat3& b) {
		Mat3 out{};
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
				for (int k = 0; k < 3; k++)
					out[r * 3 + c] += a[r * 3 + k] * b[k * 3 + c];
		return out;
	}

	Mat3 Transpose(const Mat3& a) {
		Mat3 out{};
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
				out[r * 3 + c] = a[c * 3 + r];
		return out;
	}

	std::array<double, 3> Apply(const Mat3& a, const std::array<double, 3>& v) {
		std::array<double, 3> out{};
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
				out[r] += a[r * 3 + c] * v[c];
		return out;
	}

	// maps WPILib-camera-frame vectors to OpenCV-camera-frame vectors: x_cv = -y_w, y_cv = -z_w, z_cv = x_w
	const Mat3 WpilibCameraToOpenCv{ 0, -1, 0,
		0, 0, -1,
		1, 0, 0 };

	// maps WPILib-tag-frame vectors to libapriltag-tag-frame vectors: X_w (out) = -z_a, Y_w = x_a, Z_w (up) = -y_a
	const Mat3 WpilibTagToAprilTag{ 0, 1, 0,
		0, 0, -1,
		-1, 0, 0 };
}

Pose3 AprilTagPoseToWpilib(const Pose3& cameraToTagOpenCv)
{
	Pose3 out;
	const Mat3 openCvToWpilibCamera = Transpose(WpilibCameraToOpenCv);
	out.R = Multiply(Multiply(openCvToWpilibCamera, cameraToTagOpenCv.R), WpilibTagToAprilTag);
	out.t = Apply(openCvToWpilibCamera, cameraToTagOpenCv.t);
	return out;
}

Pose3 OpenCvCameraInFieldToWpilib(const Pose3& cameraInFieldOpenCv)
{
	Pose3 out;
	out.R = Multiply(cameraInFieldOpenCv.R, WpilibCameraToOpenCv);
	out.t = cameraInFieldOpenCv.t;
	return out;
}

std::array<std::array<double, 3>, 4> WpilibTagCorners(double tagSize)
{
	const double h = tagSize / 2.0;
	return { {
		{ 0, -h, -h },
		{ 0, h, -h },
		{ 0, h, h },
		{ 0, -h, h },
	} };
}

std::array<double, 9> QuaternionToRotation(double w, double x, double y, double z)
{
	const double norm = std::sqrt(w * w + x * x + y * y + z * z);
	if (norm == 0.0) return { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	w /= norm;
	x /= norm;
	y /= norm;
	z /= norm;
	return {
		1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
		2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
		2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y),
	};
}

std::array<double, 4> RotationToQuaternion(const std::array<double, 9>& R)
{
	const double m00 = R[0], m01 = R[1], m02 = R[2];
	const double m10 = R[3], m11 = R[4], m12 = R[5];
	const double m20 = R[6], m21 = R[7], m22 = R[8];
	const double trace = m00 + m11 + m22;
	double w, x, y, z;
	if (trace > 0.0) {
		double s = std::sqrt(trace + 1.0) * 2.0;
		w = 0.25 * s;
		x = (m21 - m12) / s;
		y = (m02 - m20) / s;
		z = (m10 - m01) / s;
	} else if (m00 > m11 && m00 > m22) {
		double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
		w = (m21 - m12) / s;
		x = 0.25 * s;
		y = (m01 + m10) / s;
		z = (m02 + m20) / s;
	} else if (m11 > m22) {
		double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
		w = (m02 - m20) / s;
		x = (m01 + m10) / s;
		y = 0.25 * s;
		z = (m12 + m21) / s;
	} else {
		double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
		w = (m10 - m01) / s;
		x = (m02 + m20) / s;
		y = (m12 + m21) / s;
		z = 0.25 * s;
	}
	return { w, x, y, z };
}

}
