#pragma once
#include "SourceResult.h"
#include <optional>
#include <string>
#include <utility>
#include <cstdint>

// Shared left/right frame-pairing state: the latest unpaired frame per eye, gated on capture-time skew.
// ISink::ProcessingThreadLoop only hands Process() sources whose frame count changed, so one eye often arrives at a time.
class StereoPairer
{
public:
	enum class Outcome {
		WaitingForEye,   // this call didn't complete a pair - still missing one eye
		DroppedForSkew,  // both eyes were present but too far apart in time; the older was dropped
		Paired,          // both eyes present within maxSkewUs - .pair holds the result
	};

	struct FeedResult {
		Outcome outcome;
		int64_t skewUs = -1; // only meaningful for DroppedForSkew/Paired
		std::optional<std::pair<SourceResult, SourceResult>> pair;
	};

	StereoPairer(std::string leftSourceId, std::string rightSourceId, int64_t maxSkewUs)
		: m_LeftSourceId(std::move(leftSourceId)), m_RightSourceId(std::move(rightSourceId)), m_MaxSkewUs(maxSkewUs)
	{
	}

	// Feeds every result from one ISink::Process() call.
	FeedResult Feed(const std::vector<SourceResult>& results)
	{
		for (const auto& r : results) {
			if (r.sourceId == m_LeftSourceId) m_PendingLeft = r;
			else if (r.sourceId == m_RightSourceId) m_PendingRight = r;
		}

		if (!m_PendingLeft.has_value() || !m_PendingRight.has_value()) {
			return FeedResult{ Outcome::WaitingForEye, -1, std::nullopt };
		}

		int64_t skewUs = std::llabs(static_cast<int64_t>(m_PendingLeft->captureTimeUs) - static_cast<int64_t>(m_PendingRight->captureTimeUs));

		if (skewUs > m_MaxSkewUs) {
			// drop the older half and wait for its replacement rather than pairing a stale frame
			if (m_PendingLeft->captureTimeUs < m_PendingRight->captureTimeUs) m_PendingLeft.reset();
			else m_PendingRight.reset();
			return FeedResult{ Outcome::DroppedForSkew, skewUs, std::nullopt };
		}

		auto pair = std::make_pair(*m_PendingLeft, *m_PendingRight);
		m_PendingLeft.reset();
		m_PendingRight.reset();
		return FeedResult{ Outcome::Paired, skewUs, pair };
	}

private:
	std::string m_LeftSourceId, m_RightSourceId;
	int64_t m_MaxSkewUs;
	std::optional<SourceResult> m_PendingLeft, m_PendingRight;
};
