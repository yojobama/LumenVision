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
    void Toggle(bool threadWantedAlive);
	bool GetToggleStatus();

protected:

    virtual void Process(const std::vector<SourceResult>& sources) = 0;
    // called once Toggle(false) has stopped and joined this sink's processing thread (so it never races Process()); default no-op.
    // RecordSink overrides it to finalise the open segment so the file is playable immediately.
    virtual void OnStopped() {}
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

    bool m_ToggleState = false;

    bool m_RequireJson;
    bool m_RequireFrame;
    bool m_RequireColor;
    int m_MaxSources;

    // uint64_t to match ISource::GetCurrentFrameCount()'s return type (an int would truncate past ~2^31 frames)
    std::vector<std::pair<std::shared_ptr<ISource>, uint64_t>> m_Sources;
};

