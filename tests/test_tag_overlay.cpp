#include <catch2/catch_test_macros.hpp>
#include "TagOverlay.h"
#include <opencv2/imgproc.hpp>

namespace {

cv::Mat Camera() { return (cv::Mat_<double>(3, 3) << 800, 0, 320, 0, 800, 240, 0, 0, 1); }

int CountColour(const cv::Mat& image, const cv::Vec3b& colour)
{
	int count = 0;
	for (int y = 0; y < image.rows; y++)
		for (int x = 0; x < image.cols; x++)
			if (image.at<cv::Vec3b>(y, x) == colour) count++;
	return count;
}

// the three axes' colours are drawn solid, so a few exact-colour pixels are enough to tell they are there
const cv::Vec3b kRed(0, 0, 255), kGreen(0, 255, 0), kBlue(255, 0, 0);

}

TEST_CASE("a tag in front of the camera gets its box and its three axes drawn", "[overlay]") {
	cv::Mat image(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
	const double identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	const double translation[3] = { 0.0, 0.0, 1.0 };

	DrawTagPose(image, identity, translation, 0.2, Camera(), cv::Mat());

	CHECK(CountColour(image, kRed) > 20);
	CHECK(CountColour(image, kGreen) > 20);
	CHECK(CountColour(image, kBlue) >= 0); // a fronto-parallel tag's Z axis points straight at the lens and projects to a point
	// the X axis runs to the right of the tag's centre (800 px / metre, a 0.1 m axis is 80 px), the Y axis down
	const cv::Rect right(320, 236, 100, 8), below(316, 240, 8, 100);
	CHECK(cv::countNonZero(image(right).reshape(1)) > 0);
	CHECK(cv::countNonZero(image(below).reshape(1)) > 0);
}

TEST_CASE("a tilted tag shows its normal as the blue Z axis", "[overlay]") {
	cv::Mat image(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
	// turned 40 degrees about the vertical axis
	const double c = std::cos(0.7), s = std::sin(0.7);
	const double rotation[9] = { c, 0, s, 0, 1, 0, -s, 0, c };
	const double translation[3] = { 0.0, 0.0, 1.0 };

	DrawTagPose(image, rotation, translation, 0.2, Camera(), cv::Mat());

	CHECK(CountColour(image, kBlue) > 10);
}

TEST_CASE("nothing is drawn for a tag behind the camera", "[overlay]") {
	cv::Mat image(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
	const double identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	const double translation[3] = { 0.0, 0.0, -1.0 };

	DrawTagPose(image, identity, translation, 0.2, Camera(), cv::Mat());

	CHECK(cv::countNonZero(image.reshape(1)) == 0);
}
