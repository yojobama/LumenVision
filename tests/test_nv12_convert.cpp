#include <catch2/catch_test_macros.hpp>
#include "Nv12Convert.h"
#include <cstdlib>

namespace {

Frame FlatNv12(int width, int height, int luma)
{
	return Frame::FromNv12(cv::Mat(height, width, CV_8UC1, cv::Scalar(luma)), cv::Mat(height / 2, width / 2, CV_8UC2, cv::Scalar(128, 128)), nullptr);
}

AVFrame* AllocateFrame(AVPixelFormat format, int width, int height)
{
	AVFrame* frame = av_frame_alloc();
	frame->format = format;
	frame->width = width;
	frame->height = height;
	REQUIRE(av_frame_get_buffer(frame, 32) == 0);
	return frame;
}

// mean of a plane's bytes over its visible width x height
double PlaneMean(const AVFrame* frame, int plane, int width, int height)
{
	double sum = 0;
	for (int row = 0; row < height; row++)
		for (int col = 0; col < width; col++) sum += frame->data[plane][row * frame->linesize[plane] + col];
	return sum / (static_cast<double>(width) * height);
}

}

// The camera's picture is JPEG-range YUV and the encoders take limited range, so full-range luma 200 must land at 16 + 200 * 219 / 255 = 188.
TEST_CASE("an NV12 frame converts to either encoder format with the range squeezed into the video range", "[nv12][ffmpeg]") {
	Nv12ToAvFrame converter;
	const Frame source = FlatNv12(64, 48, 200);

	for (AVPixelFormat format : { AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P }) {
		AVFrame* dst = AllocateFrame(format, 64, 48);
		REQUIRE(converter.Convert(source, dst));
		REQUIRE(std::abs(PlaneMean(dst, 0, 64, 48) - 188.0) < 1.5);
		if (format == AV_PIX_FMT_NV12) {
			REQUIRE(std::abs(PlaneMean(dst, 1, 64, 24) - 128.0) < 1.5); // interleaved U and V
		} else {
			REQUIRE(std::abs(PlaneMean(dst, 1, 32, 24) - 128.0) < 1.5);
			REQUIRE(std::abs(PlaneMean(dst, 2, 32, 24) - 128.0) < 1.5);
		}
		av_frame_free(&dst);
	}
}

TEST_CASE("an NV12 frame is scaled to a smaller encoder frame and refuses a format it cannot fill", "[nv12][ffmpeg]") {
	Nv12ToAvFrame converter;
	AVFrame* half = AllocateFrame(AV_PIX_FMT_NV12, 32, 24);
	REQUIRE(converter.Convert(FlatNv12(64, 48, 100), half));
	REQUIRE(std::abs(PlaneMean(half, 0, 32, 24) - (16 + 100 * 219.0 / 255.0)) < 1.5);
	// the same converter handles a new source size
	REQUIRE(converter.Convert(FlatNv12(128, 96, 100), half));
	av_frame_free(&half);

	AVFrame* rgb = AllocateFrame(AV_PIX_FMT_RGB24, 64, 48);
	REQUIRE_FALSE(converter.Convert(FlatNv12(64, 48, 100), rgb));
	av_frame_free(&rgb);

	AVFrame* dst = AllocateFrame(AV_PIX_FMT_NV12, 64, 48);
	REQUIRE_FALSE(converter.Convert(Frame(cv::Mat(48, 64, CV_8UC3, cv::Scalar(1, 2, 3))), dst)); // not an NV12 frame
	av_frame_free(&dst);
}
