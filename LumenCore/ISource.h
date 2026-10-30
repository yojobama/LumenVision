#pragma once

#include <opencv2/opencv.hpp>
#include "Logger.h"
#include "SourceResult.h"
#include "StageProfile.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

// declared only: its header pulls in apriltag.h, and with it windows.h on Windows, into everything that includes ISource.h
class CameraCalibrationResult;

class ISource
{
public:
	ISource(std::shared_ptr<Logger> p_Logger, std::string m_ID);
	virtual ~ISource();
	SourceResult GetLatestResult(bool requireFrame, bool requireJson);
	// returns whatever the source last produced, without gating on which fields are present
	SourceResult GetLatestResult();
	std::string GetID();

	// The camera calibration a node's results were computed with, for a sink that passes it on (NetworkTablesSink publishes it as the node's
	// cameraIntrinsics) when the results do not carry it themselves; none by default. ObjectDetectionSink reports the one it was given.
	virtual std::optional<CameraCalibrationResult> GetCalibration();

	void Toggle(bool threadWantedAlive);
	uint64_t GetCurrentFrameCount();
	bool GetToggleStatus();

	// registers a callback invoked (off the result lock) whenever a new result is published;
	// used by bound ISinks to wake their processing thread instead of polling
	void AddResultListener(std::function<void()> listener);

	// Called by ISink::BindSource/UnbindSource so a dual-role node (ApriltagDetector/ObjectDetectionSink) can tell whether
	// anything downstream wants its FRAME (not just its JSON). isActive is called lazily so it reflects the consumer's
	// current state; it must return false once the registering sink is destroyed (guard with the alive-flag idiom).
	// wantsNow (optional) is the sharper question the detectors ask once per frame - would this consumer use a frame published right now (see
	// ISink::WantsFrameNow) - while isActive stays the steady "is anyone watching", which decides the camera's colour or grey decode.
	void RegisterFrameConsumer(const std::string& sinkId, bool requiresFrame, bool requiresColor, std::function<bool()> isActive, std::function<bool()> wantsNow = nullptr);
	// removed on an explicit UnbindSource, not on destruction (isActive then returns false forever, leaving a few dead entries)
	void UnregisterFrameConsumer(const std::string& sinkId);

	// true if at least one bound, frame-requiring consumer is currently active (alive and toggled on) and would use a frame published right now,
	// e.g. a WebRTCSink with a viewer whose pacing interval has passed. Lets detectors skip drawing annotations when only JSON is consumed (NT4),
	// and for the frames a paced preview would drop anyway.
	bool HasActiveFrameConsumer() const;
	// true if at least one bound, active consumer needs its frame in COLOUR (WebRTC/Mjpeg/Record; see ISink requireColor).
	// ApriltagDetector opts out since it only calls AsGray(), letting CameraSource request a straight-to-gray decode.
	bool HasActiveColorFrameConsumer() const;

	// Publishes at most `fps` results per second (<= 0: unlimited); the rest are dropped in SetLatestResult, so a detector
	// downstream does no work for them. Capture itself is not slowed, which keeps the frames that do get through fresh.
	void SetFpsLimit(int fps) { m_FpsLimit = fps; }
	int GetFpsLimit() const { return m_FpsLimit; }

	// Makes HasActiveFrameConsumer/HasActiveColorFrameConsumer true until the next published result that carries a frame, so a
	// detector draws its annotated output (and a camera decodes colour) for one snapshot even with nothing streaming it.
	void RequestFrameOnce() { m_FrameRequests++; }
protected:
	// this node's stage timings (shared with its ISink half when it has one); see StageProfile.h
	StageProfile* SourceProfile() const { return m_Profile.get(); }
	void SetLatestResult(SourceResult result);
	// Written under m_ResultLock (SetLatestResult) but read without it by GetCurrentFrameCount() from another thread.
	std::atomic<uint64_t> m_FrameCount{ 0 };
	virtual void CaptureFrame();
	// Invoked once each at the start/end of the capture thread's lifetime (not per frame); default no-op. Camera backends
	// override them for state that must live on the capture thread (e.g. V4L2 STREAMON/STREAMOFF).
	virtual void OnCaptureThreadStart() {}
	virtual void OnCaptureThreadStop() {}
	// whether the capture thread has been asked to stop; a CaptureFrame() that waits should check it often
	bool ShouldTerminate() const { return m_ShouldTerminate; }
	std::shared_ptr<Logger> m_Logger;
	bool m_DoNotLoadCaptureThread = false;
private:
	SourceResult m_LatestResult;

	void SourceThreadProc();
	std::mutex m_ResultLock;
	std::jthread m_Thread;
	std::atomic<bool> m_ShouldTerminate{ false };
	bool m_ToggleState = false;

	std::mutex m_ListenersMutex;
	std::vector<std::function<void()>> m_Listeners;

	struct FrameConsumer {
		bool requiresFrame;
		bool requiresColor;
		std::function<bool()> isActive;
		std::function<bool()> wantsNow; // empty: same as isActive
	};
	mutable std::mutex m_FrameConsumersMutex;
	std::unordered_map<std::string, FrameConsumer> m_FrameConsumers;

	std::atomic<int> m_FpsLimit{ -1 };
	std::atomic<int> m_FrameRequests{ 0 };
	uint64_t m_LastPublishedUs = 0; // guarded by m_ResultLock

	std::string m_ID;
	std::shared_ptr<StageProfile> m_Profile;
};

