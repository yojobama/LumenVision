#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "PixelUnpack.h"
#include <vector>

// Synthetic-buffer tests for the raw mono formats (V4L2 Y10/Y16/Y10P/Y10BPACK), checked against
// an independent reference packer, including widths off the packing group and row padding.

namespace {
	// Deterministic; covers 0 to 1023.
	uint16_t Sample10(int x, int y) { return static_cast<uint16_t>((x * 37 + y * 101 + (x ^ y) * 7) % 1024); }
	uint16_t Sample16(int x, int y) { return static_cast<uint16_t>((x * 2654435761u + y * 40503u) & 0xFFFF); }

	std::vector<uint8_t> PackY10(int w, int h, size_t stride, bool junkHighBits)
	{
		std::vector<uint8_t> buf(stride * h, 0xEE);
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++) {
				uint16_t v = Sample10(x, y) | (junkHighBits ? 0xFC00 : 0);
				buf[y * stride + 2 * x] = v & 0xFF;
				buf[y * stride + 2 * x + 1] = v >> 8;
			}
		return buf;
	}

	std::vector<uint8_t> PackY16(int w, int h, size_t stride)
	{
		std::vector<uint8_t> buf(stride * h, 0xEE);
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++) {
				uint16_t v = Sample16(x, y);
				buf[y * stride + 2 * x] = v & 0xFF;
				buf[y * stride + 2 * x + 1] = v >> 8;
			}
		return buf;
	}

	// MIPI RAW10: per 4 pixels, bytes 0-3 = each pixel's bits 9..2, byte 4 = bits 1..0 of pixels
	// 0..3 at bit positions 1:0, 3:2, 5:4, 7:6
	std::vector<uint8_t> PackY10P(int w, int h, size_t stride)
	{
		std::vector<uint8_t> buf(stride * h, 0xEE);
		for (int y = 0; y < h; y++) {
			uint8_t* row = buf.data() + y * stride;
			for (int g = 0; g * 4 < w; g++) {
				uint8_t lsbs = 0;
				for (int i = 0; i < 4; i++) {
					int x = g * 4 + i;
					uint16_t v = x < w ? Sample10(x, y) : 0;
					row[g * 5 + i] = static_cast<uint8_t>(v >> 2);
					lsbs |= static_cast<uint8_t>((v & 3) << (2 * i));
				}
				row[g * 5 + 4] = lsbs;
			}
		}
		return buf;
	}

	// Y10BPACK: a plain big-endian bit stream, 10 bits per pixel
	std::vector<uint8_t> PackY10BPack(int w, int h, size_t stride)
	{
		std::vector<uint8_t> buf(stride * h, 0);
		for (int y = 0; y < h; y++) {
			uint8_t* row = buf.data() + y * stride;
			for (int x = 0; x < w; x++) {
				uint16_t v = Sample10(x, y);
				for (int b = 0; b < 10; b++) {
					size_t bit = static_cast<size_t>(x) * 10 + b;
					if (v & (1 << (9 - b))) row[bit / 8] |= static_cast<uint8_t>(0x80 >> (bit % 8));
				}
			}
		}
		return buf;
	}

	template <typename Expected>
	void RequireGray(const cv::Mat& out, int w, int h, Expected expected)
	{
		REQUIRE(out.type() == CV_8UC1);
		REQUIRE(out.cols == w);
		REQUIRE(out.rows == h);
		int mismatches = 0;
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
				if (out.at<uint8_t>(y, x) != expected(x, y)) mismatches++;
		REQUIRE(mismatches == 0);
	}
}

TEST_CASE("PixelUnpack minimum strides follow the V4L2 format definitions", "[pixelunpack]") {
	REQUIRE(PixelUnpack::MinStrideY10(1280) == 2560);
	REQUIRE(PixelUnpack::MinStrideY10Packed(1280) == 1600); // 4 px -> 5 bytes
	REQUIRE(PixelUnpack::MinStrideY10Packed(6) == 8);       // 60 bits -> 8 bytes, partial group
	REQUIRE(PixelUnpack::MinStrideY10Packed(1) == 2);
}

TEST_CASE("PixelUnpack Y10 keeps the 8 MSBs of each LSB-aligned 10-bit sample", "[pixelunpack]") {
	auto [w, h] = GENERATE(std::pair{ 16, 4 }, std::pair{ 7, 3 });
	size_t padding = GENERATE(size_t{ 0 }, size_t{ 12 });
	bool junk = GENERATE(false, true);
	size_t stride = PixelUnpack::MinStrideY10(w) + padding;

	std::vector<uint8_t> buf = PackY10(w, h, stride, junk);
	cv::Mat out;
	PixelUnpack::Y10ToGray8(buf.data(), w, h, stride, out);
	RequireGray(out, w, h, [](int x, int y) { return static_cast<uint8_t>(Sample10(x, y) >> 2); });
}

TEST_CASE("PixelUnpack Y16 keeps the high byte", "[pixelunpack]") {
	int w = 9, h = 3;
	size_t stride = GENERATE(size_t{ 18 }, size_t{ 32 });
	std::vector<uint8_t> buf = PackY16(w, h, stride);
	cv::Mat out;
	PixelUnpack::Y16ToGray8(buf.data(), w, h, stride, out);
	RequireGray(out, w, h, [](int x, int y) { return static_cast<uint8_t>(Sample16(x, y) >> 8); });
}

TEST_CASE("PixelUnpack Y10P (MIPI RAW10) drops the packed LSB byte", "[pixelunpack]") {
	int w = GENERATE(16, 6, 1);
	size_t padding = GENERATE(size_t{ 0 }, size_t{ 3 });
	int h = 3;
	// The reference packer writes whole 5-byte groups.
	size_t stride = static_cast<size_t>((w + 3) / 4) * 5 + padding;
	std::vector<uint8_t> buf = PackY10P(w, h, stride);
	cv::Mat out;
	PixelUnpack::Y10PToGray8(buf.data(), w, h, stride, out);
	RequireGray(out, w, h, [](int x, int y) { return static_cast<uint8_t>(Sample10(x, y) >> 2); });
}

TEST_CASE("PixelUnpack Y10BPACK unpacks a big-endian 10-bit bit stream", "[pixelunpack]") {
	int w = GENERATE(16, 6, 5, 1);
	size_t padding = GENERATE(size_t{ 0 }, size_t{ 3 });
	int h = 3;
	size_t stride = PixelUnpack::MinStrideY10Packed(w) + padding;
	std::vector<uint8_t> buf = PackY10BPack(w, h, stride);
	cv::Mat out;
	PixelUnpack::Y10BPackToGray8(buf.data(), w, h, stride, out);
	RequireGray(out, w, h, [](int x, int y) { return static_cast<uint8_t>(Sample10(x, y) >> 2); });
}

TEST_CASE("PixelUnpack GREY copy honours row padding and writes into a pre-sized buffer in place", "[pixelunpack]") {
	int w = 5, h = 4;
	size_t stride = 8;
	std::vector<uint8_t> buf(stride * h, 0xEE);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) buf[y * stride + x] = static_cast<uint8_t>(y * 16 + x);

	cv::Mat out(h, w, CV_8UC1);
	const uint8_t* before = out.data;
	PixelUnpack::Gray8Copy(buf.data(), w, h, stride, out);
	REQUIRE(out.data == before); // FramePool's fast path depends on this
	RequireGray(out, w, h, [](int x, int y) { return static_cast<uint8_t>(y * 16 + x); });
}
