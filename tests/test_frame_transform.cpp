#include <catch2/catch_test_macros.hpp>
#include "FrameTransform.h"
#include <opencv2/calib3d.hpp>
#include <cmath>
#include <vector>

// Checks the transform against what a camera really does: the pixel a 3D point lands on in the transformed image must equal the point's
// projection through the transformed calibration, distortion included. The pixel mapping itself is measured by pushing marker pixels
// through FrameTransform::Apply, so the test does not repeat the implementation's arithmetic.

namespace {

struct PixelMap {
	// x' = a*x + b*y + e, y' = c*x + d*y + f
	double a, b, c, d, e, f;
	cv::Point2d operator()(cv::Point2d p) const { return { a * p.x + b * p.y + e, c * p.x + d * p.y + f }; }
};

cv::Point FindMarker(const cv::Mat& image)
{
	cv::Point where(-1, -1);
	for (int y = 0; y < image.rows; y++)
		for (int x = 0; x < image.cols; x++)
			if (image.at<uchar>(y, x) != 0) where = { x, y };
	return where;
}

cv::Point MapMarker(const FrameTransform& transform, cv::Size size, cv::Point p)
{
	cv::Mat image = cv::Mat::zeros(size, CV_8UC1);
	image.at<uchar>(p.y, p.x) = 255;
	bool view = false;
	return FindMarker(transform.Apply(image, view));
}

PixelMap MeasurePixelMap(const FrameTransform& transform, cv::Size size)
{
	const cv::Point origin(50, 40); // inside the test crop
	const cv::Point o = MapMarker(transform, size, origin);
	const cv::Point dx = MapMarker(transform, size, origin + cv::Point(1, 0)) - o;
	const cv::Point dy = MapMarker(transform, size, origin + cv::Point(0, 1)) - o;
	PixelMap map{ static_cast<double>(dx.x), static_cast<double>(dy.x), static_cast<double>(dx.y), static_cast<double>(dy.y), 0, 0 };
	map.e = o.x - (map.a * origin.x + map.b * origin.y);
	map.f = o.y - (map.c * origin.x + map.d * origin.y);
	return map;
}

}

TEST_CASE("a transformed calibration projects points where the transformed image shows them", "[frametransform]") {
	const cv::Size size(160, 120);
	CameraCalibrationResult calibration(150.0, 140.0, 82.0, 58.5, 0.3, { -0.21, 0.06, 0.004, -0.0025, 0.012 }, size.width, size.height);
	const std::vector<cv::Point3d> points = { { 0.3, -0.2, 2.0 }, { -0.5, 0.35, 1.5 }, { 0.05, 0.0, 3.0 }, { -0.2, -0.3, 1.2 } };

	for (int rotation : { 0, 90, 180, 270 }) {
		for (int flips = 0; flips < 4; flips++) {
			for (bool crop : { false, true }) {
				FrameTransform transform;
				transform.rotation = rotation;
				transform.flipHorizontal = (flips & 1) != 0;
				transform.flipVertical = (flips & 2) != 0;
				if (crop) transform = [&] { FrameTransform t = transform; t.cropX = 30; t.cropY = 10; t.cropWidth = 100; t.cropHeight = 90; return t; }();
				INFO("rotation " << rotation << " flips " << flips << " crop " << crop);

				cv::Mat marker = cv::Mat::zeros(size, CV_8UC1);
				bool view = false;
				cv::Size outSize = transform.Apply(marker, view).size();
				REQUIRE(outSize == transform.OutputSize(size));

				const PixelMap map = MeasurePixelMap(transform, size);

				CameraCalibrationResult transformed = TransformCalibration(calibration, transform);
				REQUIRE(transformed.imageWidth == outSize.width);
				REQUIRE(transformed.imageHeight == outSize.height);

				cv::Mat K = (cv::Mat_<double>(3, 3) << calibration.fx, 0, calibration.cx, 0, calibration.fy, calibration.cy, 0, 0, 1);
				cv::Mat Kt = (cv::Mat_<double>(3, 3) << transformed.fx, 0, transformed.cx, 0, transformed.fy, transformed.cy, 0, 0, 1);
				// A mirrored or rotated image is what the same scene looks like through a camera turned or mirrored the same way, so the
				// transformed model sees each point with its x/y mixed by the pixel map's linear part.
				std::vector<cv::Point3d> turned;
				for (const cv::Point3d& p : points) turned.emplace_back(map.a * p.x + map.b * p.y, map.c * p.x + map.d * p.y, p.z);

				std::vector<cv::Point2d> original, after;
				cv::projectPoints(points, cv::Vec3d(0, 0, 0), cv::Vec3d(0, 0, 0), K, calibration.distCoeffs, original);
				cv::projectPoints(turned, cv::Vec3d(0, 0, 0), cv::Vec3d(0, 0, 0), Kt, transformed.distCoeffs, after);

				for (size_t i = 0; i < points.size(); i++) {
					const cv::Point2d expected = map(original[i]);

					REQUIRE(std::abs(after[i].x - expected.x) < 1e-6);
					REQUIRE(std::abs(after[i].y - expected.y) < 1e-6);
				}
			}
		}
	}
}

TEST_CASE("an identity transform leaves frames and calibrations untouched", "[frametransform]") {
	FrameTransform identity;
	REQUIRE(identity.IsIdentity());

	cv::Mat image = cv::Mat::ones(10, 20, CV_8UC3);
	bool view = false;
	cv::Mat out = identity.Apply(image, view);
	REQUIRE(view);
	REQUIRE(out.data == image.data);

	CameraCalibrationResult calibration(100, 100, 10, 5, 0.2, { 0.1, 0.01, 0.001, 0.002, 0 }, 20, 10);
	CameraCalibrationResult same = TransformCalibration(calibration, identity);
	REQUIRE(same.cx == calibration.cx);
	REQUIRE(same.distCoeffs == calibration.distCoeffs);
}

TEST_CASE("a crop is contiguous and clipped to the frame", "[frametransform]") {
	FrameTransform crop;
	crop.cropX = 10;
	crop.cropY = 5;
	crop.cropWidth = 1000; // far larger than the frame
	crop.cropHeight = 20;
	cv::Mat image(40, 60, CV_8UC1, cv::Scalar(3));

	bool view = false;
	cv::Mat out = crop.Apply(image, view);

	REQUIRE(out.cols == 50);
	REQUIRE(out.rows == 20);
	REQUIRE(out.isContinuous());
	REQUIRE(crop.OutputSize(image.size()) == out.size());
}
