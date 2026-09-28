#pragma once
#ifdef LUMEN_WITH_CODEC_STEREO
#include "IStereoDepthBackend.h"
#include "StereoDepthBackendKind.h"
#include <cstdint>

struct cs_context; // codec_stereo/cs.h, kept out of this header

// Wraps codec-stereo's cs_extract(). Owns one cs_context for its whole lifetime; creating one per
// frame costs ~24ms/call.
class CodecStereoBackend : public IStereoDepthBackend {
public:
	struct Config {
		StereoDepthBackendKind kind = STEREO_BACKEND_CODEC_AUTO;
		int blockW = 16, blockH = 16;               // rkmpp_hwenc forces 32x16 regardless (see cs.h)
		int searchRangeX = 48, searchRangeY = 16;    // see cs.h caps.mv_min_x/max_x
		int32_t disparityOffset = 0;                 // pre-shift so the search window covers min..max depth - see StereoDepthNode
		bool invertDisparitySign = false;             // resolved once by StereoDepthNode's startup self-check

		// gating passed straight to cs_mv_field_to_disparity: min_disparity is derived by StereoDepthNode from
		// min/maxDepthMeters; a min_disparity <= 0 needs the near-zero gate effectively disabled (see cs_mv_field).
		float minDisparity = 0.0f;
		int maxDy = 4;
		uint16_t maxCost = 0; // 0 = no cost gate
	};

	explicit CodecStereoBackend(const Config& cfg);
	~CodecStereoBackend() override;

	bool Compute(const cv::Mat& rectLeft, const cv::Mat& rectRight,
		std::vector<float>& disparityOut, int& cols, int& rows) override;

	std::string Name() const override;
	int BlockW() const override { return m_Cfg.blockW; }
	int BlockH() const override { return m_Cfg.blockH; }

private:
	Config m_Cfg;
	cs_context* m_Ctx = nullptr;
};
#endif // LUMEN_WITH_CODEC_STEREO
