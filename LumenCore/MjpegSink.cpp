#include "MjpegSink.h"
#include <opencv2/opencv.hpp>

namespace {
	constexpr int MAX_BOUND_SOURCES = 1; // matches WebRTCSink's own single-source convention

	// Standard base64 (RFC 4648, '=' padding); see MjpegSink.h for why the SWIG boundary needs it.
	std::string Base64Encode(const std::vector<uint8_t>& data)
	{
		static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		out.reserve(((data.size() + 2) / 3) * 4);

		size_t i = 0;
		for (; i + 3 <= data.size(); i += 3) {
			uint32_t chunk = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
			out.push_back(alphabet[(chunk >> 18) & 0x3F]);
			out.push_back(alphabet[(chunk >> 12) & 0x3F]);
			out.push_back(alphabet[(chunk >> 6) & 0x3F]);
			out.push_back(alphabet[chunk & 0x3F]);
		}

		size_t remaining = data.size() - i;
		if (remaining == 1) {
			uint32_t chunk = static_cast<uint32_t>(data[i]) << 16;
			out.push_back(alphabet[(chunk >> 18) & 0x3F]);
			out.push_back(alphabet[(chunk >> 12) & 0x3F]);
			out.push_back('=');
			out.push_back('=');
		} else if (remaining == 2) {
			uint32_t chunk = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
			out.push_back(alphabet[(chunk >> 18) & 0x3F]);
			out.push_back(alphabet[(chunk >> 12) & 0x3F]);
			out.push_back(alphabet[(chunk >> 6) & 0x3F]);
			out.push_back('=');
		}

		return out;
	}
}

MjpegSink::MjpegSink(std::shared_ptr<Logger> logger, std::string id, int jpegQuality)
	: ISink(logger, MAX_BOUND_SOURCES, false /* requireJson */, true /* requireFrame */, id)
	, m_JpegQuality(jpegQuality)
{
}

std::string MjpegSink::GetLatestJpegBase64() const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return m_LatestJpegBase64;
}

void MjpegSink::Process(const std::vector<SourceResult>& results)
{
	for (const SourceResult& result : results) {
		if (!result.frame.has_value() || result.frame->empty()) continue;

		std::vector<uint8_t> encoded;
		std::vector<int> params{ cv::IMWRITE_JPEG_QUALITY, m_JpegQuality };
		if (!cv::imencode(".jpg", result.frame->AsBgr(), encoded, params)) continue;

		std::string base64 = Base64Encode(encoded);
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_LatestJpegBase64 = std::move(base64);
	}
}
