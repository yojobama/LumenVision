#include "FrameTransform.h"
#include <opencv2/core.hpp>
#include <algorithm>
#include <utility>

namespace {

int NormalisedRotation(int rotation)
{
	int r = rotation % 360;
	if (r < 0) r += 360;
	return (r / 90) * 90;
}

}

cv::Rect FrameTransform::EffectiveCrop(cv::Size input) const
{
	cv::Rect whole(0, 0, input.width, input.height);
	if (cropWidth <= 0 || cropHeight <= 0) return whole;
	return cv::Rect(cropX, cropY, cropWidth, cropHeight) & whole;
}

cv::Size FrameTransform::OutputSize(cv::Size input) const
{
	cv::Size cropped = EffectiveCrop(input).size();
	const int r = NormalisedRotation(rotation);
	return (r == 90 || r == 270) ? cv::Size(cropped.height, cropped.width) : cropped;
}

cv::Mat FrameTransform::Apply(const cv::Mat& input, bool& viewOfInput) const
{
	viewOfInput = true;
	cv::Mat out = input;
	const cv::Rect crop = EffectiveCrop(input.size());
	if (crop.empty()) return out;
	if (crop.size() != input.size()) {
		// downstream detectors read frames as one contiguous buffer, which a cropped view is not
		out = input(crop).clone();
		viewOfInput = false;
	}

	switch (NormalisedRotation(rotation)) {
	case 90: { cv::Mat rotated; cv::rotate(out, rotated, cv::ROTATE_90_CLOCKWISE); out = rotated; viewOfInput = false; break; }
	case 180: { cv::Mat rotated; cv::rotate(out, rotated, cv::ROTATE_180); out = rotated; viewOfInput = false; break; }
	case 270: { cv::Mat rotated; cv::rotate(out, rotated, cv::ROTATE_90_COUNTERCLOCKWISE); out = rotated; viewOfInput = false; break; }
	default: break;
	}

	if (flipHorizontal || flipVertical) {
		cv::Mat flipped;
		cv::flip(out, flipped, flipHorizontal && flipVertical ? -1 : (flipHorizontal ? 1 : 0));
		out = flipped;
		viewOfInput = false;
	}
	return out;
}

CameraCalibrationResult TransformCalibration(const CameraCalibrationResult& calibration, const FrameTransform& transform)
{
	if (transform.IsIdentity() || calibration.imageWidth <= 0 || calibration.imageHeight <= 0) return calibration;

	CameraCalibrationResult out = calibration;
	double width = calibration.imageWidth;
	double height = calibration.imageHeight;

	// distortion terms: transposing swaps p1/p2, a horizontal mirror negates p2, a vertical mirror negates p1
	auto tangential = [&](bool swap, bool negateP1, bool negateP2) {
		if (out.distCoeffs.size() < 4) return;
		if (swap) std::swap(out.distCoeffs[2], out.distCoeffs[3]);
		if (negateP1) out.distCoeffs[2] = -out.distCoeffs[2];
		if (negateP2) out.distCoeffs[3] = -out.distCoeffs[3];
	};
	auto transpose = [&]() {
		std::swap(out.cx, out.cy);
		std::swap(out.fx, out.fy);
		std::swap(width, height);
		tangential(true, false, false);
	};
	auto mirrorHorizontal = [&]() {
		out.cx = width - 1.0 - out.cx;
		tangential(false, false, true);
	};
	auto mirrorVertical = [&]() {
		out.cy = height - 1.0 - out.cy;
		tangential(false, true, false);
	};

	// crop first: the principal point moves with the pixels
	const cv::Rect crop = transform.EffectiveCrop(cv::Size(calibration.imageWidth, calibration.imageHeight));
	out.cx -= crop.x;
	out.cy -= crop.y;
	width = crop.width;
	height = crop.height;

	// a clockwise quarter turn is a transpose then a horizontal mirror; the counter-clockwise one a transpose then a vertical mirror
	switch (NormalisedRotation(transform.rotation)) {
	case 90: transpose(); mirrorHorizontal(); break;
	case 180: mirrorHorizontal(); mirrorVertical(); break;
	case 270: transpose(); mirrorVertical(); break;
	default: break;
	}
	if (transform.flipHorizontal) mirrorHorizontal();
	if (transform.flipVertical) mirrorVertical();

	out.imageWidth = static_cast<int>(width);
	out.imageHeight = static_cast<int>(height);
	return out;
}
