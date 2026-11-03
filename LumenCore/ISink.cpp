#include "ISink.h"
#include "CpuAffinity.h"
#include <algorithm>
#include <iostream>

ISink::ISink(std::shared_ptr<Logger> p_Logger, int maxSources, bool requireJson, bool requireFrame, std::string id, bool requireColor) : m_Logger(p_Logger) {
    if (m_Logger) m_Logger->EnterLog("ISink constructed");
    m_MaxSources = maxSources;
    m_RequireJson = requireJson;
    m_RequireFrame = requireFrame;
    m_RequireColor = requireColor;
    m_ID = id;
    m_Profile = NodeProfiles::For(id);
}

ISink::~ISink()
{
    if (m_Thread.joinable()) {
        m_ShouldTerminate = true;
        {
            std::lock_guard<std::mutex> guard(m_Wake->mutex);
            m_Wake->dataAvailable = true;
        }
        m_Wake->cv.notify_one();
        m_Thread.join();
    }
    // Leave every source before this object goes: UnregisterFrameConsumer waits for a consumer callback that is running on the source's
    // thread, so nothing calls into this sink once it returns, and the wake listener is gone with it.
    std::vector<Binding> bindings;
    {
        std::lock_guard<std::mutex> guard(m_SourcesMutex);
        bindings.swap(m_Sources);
    }
    for (const Binding& binding : bindings) Detach(binding);
}

void ISink::Toggle(bool toggle)
{
    std::lock_guard<std::mutex> toggleGuard(m_ToggleMutex);
    // idempotent: a repeated Toggle(true) must not spawn a second processing thread
    if (toggle == m_ToggleState) return;

    if (toggle) {
        m_ShouldTerminate = false;
        m_Thread = std::jthread([this] { ProcessingThreadLoop(); });
        m_ToggleState = true;
    }
    else {
        if (m_Thread.joinable()) {
            m_ShouldTerminate = true;
            {
                // wake the loop immediately so it observes m_ShouldTerminate instead of
                // waiting out its full timeout before it can be joined
                std::lock_guard<std::mutex> guard(m_Wake->mutex);
                m_Wake->dataAvailable = true;
            }
            m_Wake->cv.notify_one();
            m_Thread.join();
            m_ToggleState = false;
            OnStopped();
        }
    }
}

bool ISink::GetToggleStatus()
{
    return m_ToggleState;
}

void ISink::NotifyDataAvailable()
{
    std::lock_guard<std::mutex> guard(m_Wake->mutex);
    m_Wake->dataAvailable = true;
    m_Wake->cv.notify_one();
}

std::vector<ISink::Binding> ISink::CopyBindings() const
{
    std::lock_guard<std::mutex> guard(m_SourcesMutex);
    return m_Sources;
}

void ISink::ProcessingThreadLoop()
{
    // heavy and latency-sensitive: keep off the efficiency cores on big.LITTLE (see CpuAffinity)
    CpuAffinity::PinCurrentThreadToPerformanceCores();
    CpuAffinity::NameCurrentThread("snk", m_ID);

    // safety-net poll interval: if a bound source stalls or a notification is missed,
    // the loop still re-checks frame counts periodically instead of hanging forever
    const auto pollTimeout = std::chrono::milliseconds(100);

    // A node that throws on one frame (a bad calibration, a model that fails) must not take the whole service down with it:
    // the frame is dropped and the loop goes on. The same message is logged once, then at most every 5 s with a count of what was skipped.
    std::string lastError;
    auto lastErrorLoggedAt = std::chrono::steady_clock::time_point{};
    long long suppressed = 0;
    auto report = [&](const std::string& what) {
        const auto now = std::chrono::steady_clock::now();
        if (what == lastError && now - lastErrorLoggedAt < std::chrono::seconds(5)) { suppressed++; return; }
        std::string line = "node " + m_ID + " dropped a frame: " + what;
        if (suppressed > 0) line += " (" + std::to_string(suppressed) + " more like it since the last report)";
        if (m_Logger) m_Logger->EnterLog(LogLevel::Error, line);
        else std::cerr << line << std::endl;
        lastError = what;
        lastErrorLoggedAt = now;
        suppressed = 0;
    };

    while (!m_ShouldTerminate) {
        {
            std::unique_lock<std::mutex> lock(m_Wake->mutex);
            m_Wake->cv.wait_for(lock, pollTimeout, [this] { return m_Wake->dataAvailable || m_ShouldTerminate.load(); });
            m_Wake->dataAvailable = false;
        }

        if (m_ShouldTerminate) break;

        // the bindings are copied once per wake-up, so BindSource/UnbindSource on another thread never changes what this loop iterates, and a
        // source that has just been unbound stays alive for the rest of this pass
        std::vector<SourceResult> sources;
        for (const Binding& binding : CopyBindings()) {
            const uint64_t frameCount = binding.source->GetCurrentFrameCount();
            if (frameCount != binding.lastFrameCount->load()) {
                binding.lastFrameCount->store(frameCount);
                SourceResult sourceResult = binding.source->GetLatestResult(m_RequireFrame, m_RequireJson);
                sourceResult.sourceId = binding.source->GetID();
                sources.push_back(sourceResult);
            }
        }

        try {
            if (!sources.empty()) {
                // "wait": how long the newest result sat before this sink got to it (a busy sink, or a thread that was not scheduled);
                // "process": what this sink then spent on it
                const uint64_t pickedUpUs = SourceResult::NowUs();
                uint64_t newestProducedUs = 0;
                for (const SourceResult& source : sources) newestProducedUs = std::max(newestProducedUs, source.producedTimeUs);
                if (newestProducedUs != 0 && pickedUpUs >= newestProducedUs) m_Profile->Record("wait", static_cast<double>(pickedUpUs - newestProducedUs));

                ScopedStage processing(m_Profile.get(), "process");
                Process(sources);
            }
            OnPoll();
        }
        catch (const std::exception& error) {
            report(error.what());
        }
        catch (...) {
            report("unknown exception");
        }
    }
    m_ShouldTerminate = false;
}

std::string ISink::GetID()
{
    return m_ID;
}

bool ISink::BindSource(std::shared_ptr<ISource> p_Source) {
    if (m_Logger) m_Logger->EnterLog("ISink::BindSource called");

    if (!p_Source) {
        if (m_Logger) m_Logger->EnterLog(LogLevel::Error, "ISink::BindSource: source is null");
        return false;
    }

    Binding binding;
    binding.source = p_Source;
    binding.lastFrameCount = std::make_shared<std::atomic<uint64_t>>(0);
    {
        std::lock_guard<std::mutex> guard(m_SourcesMutex);
        for (const Binding& bound : m_Sources) {
            if (bound.source == p_Source) return true; // already bound: a second binding would deliver every result twice
        }
        if (static_cast<int>(m_Sources.size()) >= m_MaxSources) return false;
        // The listener only wakes this sink's WakeState; the weak_ptr means a stale call after the sink is gone touches nothing.
        std::weak_ptr<WakeState> wake = m_Wake;
        binding.listener = p_Source->AddResultListener([wake] {
            if (auto state = wake.lock()) {
                std::lock_guard<std::mutex> guard(state->mutex);
                state->dataAvailable = true;
                state->cv.notify_one();
            }
        });
        m_Sources.push_back(binding);
    }
    // see ISource::HasActiveFrameConsumer: lets a bound detector know whether to annotate for this sink. The callbacks run on the source's
    // thread and use `this`; ~ISink and UnbindSource unregister them first, and UnregisterFrameConsumer waits for a running one, so they
    // never see a destroyed sink.
    p_Source->RegisterFrameConsumer(m_ID, m_RequireFrame, m_RequireColor, [this] {
        return this->GetToggleStatus() && this->IsConsumingFrames();
    }, [this] {
        return this->GetToggleStatus() && this->WantsFrameNow();
    });
    return true;
}

bool ISink::IsBoundTo(const std::string& sourceID) const {
    std::lock_guard<std::mutex> guard(m_SourcesMutex);
    for (const auto& bound : m_Sources) {
        if (bound.source->GetID() == sourceID) return true;
    }
    return false;
}

std::vector<std::shared_ptr<ISource>> ISink::GetBoundSources() const {
    std::vector<std::shared_ptr<ISource>> sources;
    std::lock_guard<std::mutex> guard(m_SourcesMutex);
    for (const auto& bound : m_Sources) sources.push_back(bound.source);
    return sources;
}

std::shared_ptr<ISource> ISink::FindBoundSource(const std::string& sourceID) const {
    std::lock_guard<std::mutex> guard(m_SourcesMutex);
    for (const auto& bound : m_Sources) {
        if (bound.source->GetID() == sourceID) return bound.source;
    }
    return nullptr;
}

void ISink::Detach(const Binding& binding) {
    binding.source->RemoveResultListener(binding.listener);
    binding.source->UnregisterFrameConsumer(m_ID);
}

bool ISink::UnbindSource(std::string sourceID) {
    if (m_Logger) m_Logger->EnterLog("ISink::UnbindSource called");

    Binding removed;
    {
        std::lock_guard<std::mutex> guard(m_SourcesMutex);
        auto it = std::find_if(m_Sources.begin(), m_Sources.end(), [&](const Binding& b) { return b.source->GetID() == sourceID; });
        if (it == m_Sources.end()) return false;
        removed = *it;
        m_Sources.erase(it);
    }
    Detach(removed);
    return true;
}

bool ISink::UnbindAllSources() {
    std::vector<Binding> removed;
    {
        std::lock_guard<std::mutex> guard(m_SourcesMutex);
        removed.swap(m_Sources);
    }
    for (const Binding& binding : removed) Detach(binding);
    return !removed.empty();
}
