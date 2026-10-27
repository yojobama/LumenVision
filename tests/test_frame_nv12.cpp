#include <catch2/catch_test_macros.hpp>
#include "Frame.h"
#include <opencv2/imgproc.hpp>

namespace {

// An NV12 picture laid out the way the hardware decoder leaves it: rows padded to a 16-pixel stride, and the chroma plane starting after
// verStride luma rows (more than the picture's height).
struct DecoderBuffer {
	static constexpr int width = 40, height = 20, horStride = 48, verStride = 32;
	std::shared_ptr<std::vector<uint8_t>> bytes = std::make_shared<std::vector<uint8_t>>(horStride * verStride * 3 / 2, 0);

	DecoderBuffer()
	{
		for (int row = 0; row < height; row++)
			for (int col = 0; col < width; col++) (*bytes)[row * horStride + col] = static_cast<uint8_t>(16 + row * 8 + col);
		uint8_t* uv = bytes->data() + horStride * verStride;
		for (int row = 0; row < height / 2; row++)
			for (int col = 0; col < width / 2; col++) {
				uv[row * horStride + col * 2] = static_cast<uint8_t>(100 + col);     // U
				uv[row * horStride + col * 2 + 1] = static_cast<uint8_t>(150 + row); // V
			}
	}

	Frame MakeFrame(std::shared_ptr<void> owner) const
	{
		cv::Mat y(height, width, CV_8UC1, bytes->data(), horStride);
		cv::Mat uv(height / 2, width / 2, CV_8UC2, bytes->data() + horStride * verStride, horStride);
		return Frame::FromNv12(y, uv, std::move(owner));
	}

	// the same picture as one contiguous NV12 block, for cv::cvtColor
	cv::Mat Packed() const
	{
		cv::Mat packed(height * 3 / 2, width, CV_8UC1);
		for (int row = 0; row < height; row++) std::memcpy(packed.ptr(row), bytes->data() + row * horStride, width);
		const uint8_t* uv = bytes->data() + horStride * verStride;
		for (int row = 0; row < height / 2; row++) std::memcpy(packed.ptr(height + row), uv + row * horStride, width);
		return packed;
	}
};

}

TEST_CASE("an NV12 frame reads its luma plane in place and converts the strided planes correctly", "[frame][nv12]") {
	DecoderBuffer buffer;
	Frame frame = buffer.MakeFrame(nullptr);

	REQUIRE(frame.IsNv12());
	REQUIRE(frame.format() == FrameFormat::NV12);
	REQUIRE(frame.size() == cv::Size(DecoderBuffer::width, DecoderBuffer::height));

	// grey is the luma plane itself, not a copy
	REQUIRE(frame.AsGray().data == buffer.bytes->data());
	REQUIRE(frame.AsGray().size() == frame.size());

	// the planes are full (JPEG) range: squeezed to video range first, OpenCV's video-range conversion gives the expected picture
	cv::Mat packed = buffer.Packed(), squeezed(packed.size(), CV_8UC1), expected;
	for (int row = 0; row < packed.rows; row++)
		for (int col = 0; col < packed.cols; col++) {
			const int v = packed.at<uchar>(row, col);
			squeezed.at<uchar>(row, col) = row < DecoderBuffer::height ? cv::saturate_cast<uchar>(16.0 + v * 219.0 / 255.0)
				: cv::saturate_cast<uchar>(128.0 + (v - 128) * 224.0 / 255.0);
		}
	cv::cvtColor(squeezed, expected, cv::COLOR_YUV2BGR_NV12);
	REQUIRE(cv::norm(frame.AsBgr(), expected, cv::NORM_INF) == 0.0);

	cv::Mat copy;
	frame.CopyBgrTo(copy);
	REQUIRE(cv::norm(copy, expected, cv::NORM_INF) == 0.0);
	REQUIRE(copy.data != frame.AsBgr().data); // a private copy to draw on
}

TEST_CASE("an NV12 frame keeps its buffer alive for as long as any copy of it lives", "[frame][nv12]") {
	DecoderBuffer buffer;
	bool released = false;
	std::shared_ptr<void> owner(buffer.bytes.get(), [&released](void*) { released = true; });
	{
		Frame frame = buffer.MakeFrame(owner);
		owner.reset();
		Frame copy = frame; // a refcount bump
		REQUIRE_FALSE(released);
		Frame roi = frame.Roi(cv::Rect(4, 2, 16, 8));
		REQUIRE_FALSE(released);
	}
	REQUIRE(released);
}

TEST_CASE("an NV12 region is cut from both planes when its edges are even, and comes back as BGR otherwise", "[frame][nv12]") {
	DecoderBuffer buffer;
	Frame frame = buffer.MakeFrame(nullptr);
	cv::Mat whole;
	frame.CopyBgrTo(whole);

	Frame even = frame.Roi(cv::Rect(4, 2, 16, 8));
	REQUIRE(even.IsNv12());
	REQUIRE(even.size() == cv::Size(16, 8));
	REQUIRE(even.AsGray().data == buffer.bytes->data() + 2 * DecoderBuffer::horStride + 4);
	cv::Mat evenBgr;
	even.CopyBgrTo(evenBgr);
	// the chroma of a region is the chroma of the same pixels in the whole picture
	REQUIRE(cv::norm(evenBgr, whole(cv::Rect(4, 2, 16, 8)), cv::NORM_INF) == 0.0);

	Frame odd = frame.Roi(cv::Rect(5, 3, 15, 7));
	REQUIRE_FALSE(odd.IsNv12());
	REQUIRE(odd.format() == FrameFormat::BGR24);
	REQUIRE(odd.size() == cv::Size(15, 7));
	REQUIRE(cv::norm(odd.AsBgr(), whole(cv::Rect(5, 3, 15, 7)), cv::NORM_INF) == 0.0);
}

TEST_CASE("FromNv12 refuses planes that do not fit together", "[frame][nv12]") {
	cv::Mat y(20, 40, CV_8UC1), uv(10, 20, CV_8UC2);
	REQUIRE_NOTHROW(Frame::FromNv12(y, uv, nullptr));
	REQUIRE_THROWS(Frame::FromNv12(y, cv::Mat(9, 20, CV_8UC2), nullptr));
	REQUIRE_THROWS(Frame::FromNv12(cv::Mat(20, 40, CV_8UC3), uv, nullptr));
	REQUIRE_THROWS(Frame::FromNv12(cv::Mat(21, 40, CV_8UC1), cv::Mat(10, 20, CV_8UC2), nullptr));
}

// The NV12 frames come from the JPEG decoder, so their values are full range: black is 0 and white 255, as a JPEG decoder converts them. OpenCV's
// NV12 conversion alone treats them as video range (Y 16 black, 235 white) and stretched every camera picture's contrast.
TEST_CASE("an NV12 frame converts to BGR as full-range YUV, as a JPEG decoder does", "[frame][nv12]") {
	// greys: every luma level with neutral chroma comes back as the same level, to within one
	for (int level = 0; level < 256; level += 5) {
		cv::Mat y(4, 4, CV_8UC1, cv::Scalar(level)), uv(2, 2, CV_8UC2, cv::Scalar(128, 128));
		const cv::Vec3b pixel = Frame::FromNv12(y, uv, nullptr).AsBgr().at<cv::Vec3b>(0, 0);
		for (int c = 0; c < 3; c++) REQUIRE(std::abs(pixel[c] - level) <= 1);
	}
	// colours: the JFIF (full-range BT.601) formula, to within two levels
	for (int u : { 64, 128, 192 })
		for (int v : { 64, 128, 192 }) {
			const int luma = 120;
			cv::Mat y(4, 4, CV_8UC1, cv::Scalar(luma)), uv(2, 2, CV_8UC2, cv::Scalar(u, v));
			cv::Mat copy;
			Frame::FromNv12(y, uv, nullptr).CopyBgrTo(copy);
			const cv::Vec3b pixel = copy.at<cv::Vec3b>(0, 0);
			const double r = luma + 1.402 * (v - 128), g = luma - 0.344136 * (u - 128) - 0.714136 * (v - 128), b = luma + 1.772 * (u - 128);
			REQUIRE(std::abs(pixel[2] - cv::saturate_cast<uchar>(r)) <= 2);
			REQUIRE(std::abs(pixel[1] - cv::saturate_cast<uchar>(g)) <= 2);
			REQUIRE(std::abs(pixel[0] - cv::saturate_cast<uchar>(b)) <= 2);
		}
}
