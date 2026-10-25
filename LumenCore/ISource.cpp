#include "ISource.h"
#include "SourceResult.h"
#include "CpuAffinity.h"

ISource::ISource(std::shared_ptr<Logger> p_Logger, std::string m_ID)
	: m_ResultLock()
{
	this->m_Logger = p_Logger;
	this->m_ID = m_ID;
	this->m_Profile = NodeProfiles::For(m_ID);
}

ISource::~ISource()
{
	if (m_Thread.joinable()) {
		m_ShouldTerminate = true;
		m_Thread.join();
	}
}

std::string ISource::GetID()
{
	return m_ID;
}

void ISource::Toggle(bool threadWantedAlive)
{
	if (m_DoNotLoadCaptureThread) return;
	// idempotent: a repeated Toggle(true) must not spawn a second capture thread
	if (threadWantedAlive == m_ToggleState) return;

	if (threadWantedAlive) {
		m_ShouldTerminate = false;
		m_Thread = std::jthread([this] { SourceThreadProc(); });
		m_ToggleState = true;
	} else {
		if (m_Thread.joinable()) {
			m_ShouldTerminate = true;
			m_Thread.join();
			m_ToggleState = false;
		}
	}
}

uint64_t ISource::GetCurrentFrameCount()
{
	return m_FrameCount;
}

bool ISource::GetToggleStatus()
{
	return m_ToggleState;
}

void ISource::AddResultListener(std::function<void()> listener)
{
	std::lock_guard<std::mutex> guard(m_ListenersMutex);
	m_Listeners.push_back(std::move(listener));
}

void ISource::RegisterFrameConsumer(const std::string& sinkId, bool requiresFrame, bool requiresColor, std::function<bool()> isActive, std::function<bool()> wantsNow)
{
	std::lock_guard<std::mutex> guard(m_FrameConsumersMutex);
	m_FrameConsumers[sinkId] = FrameConsumer{ requiresFrame, requiresColor, std::move(isActive), std::move(wantsNow) };
}

void ISource::UnregisterFrameConsumer(const std::string& sinkId)
{
	std::lock_guard<std::mutex> guard(m_FrameConsumersMutex);
	m_FrameConsumers.erase(sinkId);
}

bool ISource::HasActiveFrameConsumer() const
{
	if (m_FrameRequests > 0) return true;
	std::lock_guard<std::mutex> guard(m_FrameConsumersMutex);
	for (const auto& [id, consumer] : m_FrameConsumers) {
		if (consumer.requiresFrame && (consumer.wantsNow ? consumer.wantsNow() : consumer.isActive())) return true;
	}
	return false;
}

bool ISource::HasActiveColorFrameConsumer() const
{
	if (m_FrameRequests > 0) return true;
	std::lock_guard<std::mutex> guard(m_FrameConsumersMutex);
	for (const auto& [id, consumer] : m_FrameConsumers) {
		if (consumer.requiresFrame && consumer.requiresColor && consumer.isActive()) return true;
	}
	return false;
}

void ISource::SetLatestResult(SourceResult result)
{
	const bool hasFrame = result.frame.has_value();
	double ageUs = -1, intervalUs = -1; // recorded once the result is published, outside the result lock
	{
		std::lock_guard<std::mutex> guard(m_ResultLock);
		result.producedTimeUs = SourceResult::NowUs();

		// FPS limiter: drop a result that follows the last published one by less than ~the limit's interval (the 0.9
		// keeps a source running exactly at the limit from losing every other frame to timing jitter)
		const int fpsLimit = m_FpsLimit;
		if (fpsLimit > 0 && m_LastPublishedUs != 0 &&
			static_cast<double>(result.producedTimeUs - m_LastPublishedUs) < 0.9 * 1'000'000.0 / fpsLimit) {
			return;
		}
		if (m_LastPublishedUs != 0) intervalUs = static_cast<double>(result.producedTimeUs - m_LastPublishedUs);
		m_LastPublishedUs = result.producedTimeUs;
		// a producer without an explicit capture timestamp gets producedTimeUs as captureTimeUs (see SourceResult.h)
		if (result.captureTimeUs == 0) result.captureTimeUs = result.producedTimeUs;
		result.frameNumber = m_FrameCount + 1; // matches the m_FrameCount++ below
		ageUs = static_cast<double>(result.producedTimeUs) - static_cast<double>(result.captureTimeUs);
		m_LatestResult = result;
		// m_FrameCount is what ISink::ProcessingThreadLoop checks to see whether a source has anything new, so it is
		// bumped here on every published result rather than in each subclass.
		m_FrameCount++;
	}
	if (hasFrame) m_FrameRequests = 0;

	// "age": how old the frame is as this node publishes it - the latency the UI shows - and "interval": time since the previous result
	if (m_Profile && ageUs >= 0) {
		m_Profile->Record("age", ageUs);
		if (intervalUs >= 0) m_Profile->Record("interval", intervalUs);
	}

	// notify bound sinks outside the result lock so a listener can safely call back
	// into this source (e.g. GetLatestResult) without deadlocking
	std::vector<std::function<void()>> listenersCopy;
	{
		std::lock_guard<std::mutex> guard(m_ListenersMutex);
		listenersCopy = m_Listeners;
	}
	for (auto& listener : listenersCopy) {
		listener();
	}
}

SourceResult ISource::GetLatestResult(bool requireFrame, bool requireJson)
{
	std::lock_guard<std::mutex> guard(m_ResultLock);
	// "at least", not "exactly": a sink declares what it needs, not what the source may also produce.
	if ((!requireJson || m_LatestResult.json.has_value()) && (!requireFrame || m_LatestResult.frame.has_value()))
		return m_LatestResult;
	return SourceResult();
}

SourceResult ISource::GetLatestResult()
{
	std::lock_guard<std::mutex> guard(m_ResultLock);
	return m_LatestResult;
}

void ISource::SourceThreadProc()
{
	// capture + colour conversion is CPU-heavy and latency-sensitive: keep off the efficiency cores on big.LITTLE (see CpuAffinity)
	CpuAffinity::PinCurrentThreadToPerformanceCores();
	CpuAffinity::NameCurrentThread("src", m_ID);
	OnCaptureThreadStart();
	while (!m_ShouldTerminate) {
		CaptureFrame();
	}
	OnCaptureThreadStop();
	m_ShouldTerminate = false;
}

void ISource::CaptureFrame() {
}
