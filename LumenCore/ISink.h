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

    bool BindSource(std::shared_ptr<ISource> p_Source);
	bool UnbindSource(std::string sourceID);
	bool IsBoundTo(const std::string& sourceID) const;
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
private:
    std::shared_ptr<Logger> m_Logger;

    void ProcessingThreadLoop();
    // called (via a listener registered on each bound source) whenever any bound source
    // publishes a new result; wakes ProcessingThreadLoop instead of it polling frame counts
    void NotifyDataAvailable();

    std::jthread m_Thread;
    std::atomic<bool> m_ShouldTerminate{ false };

    std::mutex m_WakeMutex;
    std::condition_variable m_WakeCV;
    bool m_DataAvailable = false;

    // shared with the lambdas registered via ISource::AddResultListener; flipped to false in
    // ~ISink so a listener firing after this sink is destroyed does not touch a dangling `this`
    std::shared_ptr<std::atomic<bool>> m_AliveFlag = std::make_shared<std::atomic<bool>>(true);

    std::string m_ID;
    std::shared_ptr<StageProfile> m_Profile;

    bool m_ToggleState = false;

    bool m_RequireJson;
    bool m_RequireFrame;
    bool m_RequireColor;
    int m_MaxSources;

    // uint64_t to match ISource::GetCurrentFrameCount()'s return type (an int would truncate past ~2^31 frames)
    std::vector<std::pair<std::shared_ptr<ISource>, uint64_t>> m_Sources;
};

