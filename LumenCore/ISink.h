#pragma once

#include "Logger.h"
#include <vector>
#include "ISource.h"
#include <string>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <thread>
#include "SourceResult.h"
#include "StageProfile.h"

class ISink
{
public:
    // requireColor: whether Process() calls Frame::AsBgr() on its input; default true. Only meaningful when requireFrame is
    // also true. See ISource::HasActiveColorFrameConsumer; ApriltagDetector opts out (it only calls AsGray()).
    ISink(std::shared_ptr<Logger> p_Logger, int maxSources, bool requireJson, bool requireFrame, std::string id, bool requireColor = true);
    virtual ~ISink();

    std::string GetID();

    // Binding a source that is already bound succeeds without binding it again (a second binding would hand every result over twice).
    bool BindSource(std::shared_ptr<ISource> p_Source);
	bool UnbindSource(std::string sourceID);
	// removes every bound source; returns whether there was any
	bool UnbindAllSources();
	bool IsBoundTo(const std::string& sourceID) const;
	// the sources bound right now, in bind order (a snapshot: safe to use while another thread binds or unbinds)
	std::vector<std::shared_ptr<ISource>> GetBoundSources() const;
    void Toggle(bool threadWantedAlive);
	bool GetToggleStatus();
	// Whether anything is receiving this sink's output right now. A toggled-on preview sink with no viewer returns false, which lets
	// the camera decode in grey and detectors skip drawing (see ISource::HasActiveFrameConsumer). Called from the source's thread
	// for every frame, so it must be cheap.
	virtual bool IsConsumingFrames() const { return true; }
	// Whether this sink would take a frame published right now: IsConsumingFrames() and, for a sink that uses fewer frames than its source makes
	// (a preview paced at its stream rate), the pacing interval has passed. Detectors draw and convert their annotated frame only for a sink that
	// wants it, so a 10 fps preview of a 60 fps camera costs the overlay ten times a second, not sixty. Called from the source's thread for
	// every frame, so it must be cheap. Defaults to IsConsumingFrames().
	virtual bool WantsFrameNow() const { return IsConsumingFrames(); }

protected:
    // this node's stage timings (shared with its ISource half when it has one); see StageProfile.h
    StageProfile* SinkProfile() const { return m_Profile.get(); }

    virtual void Process(const std::vector<SourceResult>& sources) = 0;
    // called once Toggle(false) has stopped and joined this sink's processing thread (so it never races Process()); default no-op.
    // RecordSink overrides it to finalise the open segment so the file is playable immediately.
    virtual void OnStopped() {}
    // called on the processing thread after every wake-up, with or without new results (at least every 100 ms while running), so a sink can
    // publish on a clock of its own; default no-op. NetworkTablesSink publishes the board's status from it.
    virtual void OnPoll() {}
    // The bound source with this id, or null; for Process(), which only sees results. Call it on the processing thread (as Process()).
    std::shared_ptr<ISource> FindBoundSource(const std::string& sourceID) const;
private:
    std::shared_ptr<Logger> m_Logger;

    void ProcessingThreadLoop();
    // called (via a listener registered on each bound source) whenever any bound source
    // publishes a new result; wakes ProcessingThreadLoop instead of it polling frame counts
    void NotifyDataAvailable();

    std::jthread m_Thread;
    std::atomic<bool> m_ShouldTerminate{ false };
    std::mutex m_ToggleMutex; // makes Toggle's check-then-act atomic: two threads enabling at once must not both start a thread

    // What the result listeners touch. It is shared with them (by weak_ptr), so a source that is still calling a listener while this sink is
    // being destroyed wakes nothing that has been freed; ~ISink also removes the listeners, so this is the second line of defence.
    struct WakeState {
        std::mutex mutex;
        std::condition_variable cv;
        bool dataAvailable = false;
    };
    std::shared_ptr<WakeState> m_Wake = std::make_shared<WakeState>();

    std::string m_ID;
    std::shared_ptr<StageProfile> m_Profile;

    std::atomic<bool> m_ToggleState{ false };

    bool m_RequireJson;
    bool m_RequireFrame;
    bool m_RequireColor;
    int m_MaxSources;

    // One bound source. lastFrameCount is shared so the processing thread can work on a copy of the bindings while the vector itself is
    // changed by BindSource/UnbindSource on another thread. uint64_t to match ISource::GetCurrentFrameCount()'s return type.
    struct Binding {
        std::shared_ptr<ISource> source;
        std::shared_ptr<std::atomic<uint64_t>> lastFrameCount;
        ISource::ListenerId listener = 0;
    };
    // guarded by m_SourcesMutex
    mutable std::mutex m_SourcesMutex;
    std::vector<Binding> m_Sources;
    // takes the binding off its source (listener and frame consumer); call without m_SourcesMutex held
    void Detach(const Binding& binding);
    // the processing loop's view of the bindings
    std::vector<Binding> CopyBindings() const;
};

