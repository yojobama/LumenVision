#pragma once
#include "ObjectDetection.h"
#include "ObjectTrackingConfig.h"
#include <opencv2/core.hpp>
#include <cstdint>
#include <vector>

// What the tracker says about one detection of the frame.
struct TrackInfo {
	int trackId = -1;          // -1 while the object is still unconfirmed
	double vxPxPerS = 0.0;     // velocity of the box centre, pixels per second
	double vyPxPerS = 0.0;
};

// One tracker per detector. Not thread-safe: the sink calls it from its processing thread and guards SetConfig itself.
//
// Time is the frames' capture time, not a frame count: cameras run at 30 to 120 fps and drop frames, and a velocity has to be in pixels per second
// whichever it is. Each frame the tracks are moved to their predicted place, the detections are matched to them with the optimal assignment
// (cv::linearAssignment on 1 - IoU, a pair below minIoU forbidden), matched tracks are corrected, unmatched detections start tentative tracks and
// tracks unseen for maxAgeMs end.
class ObjectTracker {
public:
	explicit ObjectTracker(ObjectTrackingConfig config = {});

	// Takes effect from the next Update; tracks are kept (the ids already handed out stay valid).
	void SetConfig(const ObjectTrackingConfig& config);
	const ObjectTrackingConfig& Config() const { return m_Config; }

	// One TrackInfo per detection, in the detections' order. frameSize is the detector's frame: a change of it, or capture time going backwards,
	// starts over (the tracks belong to another picture).
	std::vector<TrackInfo> Update(const std::vector<ObjectDetection>& detections, uint64_t captureTimeUs, cv::Size frameSize);

	// forgets every track (the ids already handed out are not reused)
	void Reset();

	size_t TrackCount() const { return m_Tracks.size(); }

private:
	struct Track {
		int classId = 0;
		cv::Point2d centre;
		cv::Size2d size;
		cv::Point2d velocity;       // pixels per second
		uint64_t lastSeenUs = 0;    // capture time of the last matched detection
		int observations = 0;       // matched detections so far, the creating one included
		int publishedId = -1;       // -1 until confirmed
	};

	static double IoU(const cv::Rect2d& a, const cv::Rect2d& b);

	ObjectTrackingConfig m_Config;
	std::vector<Track> m_Tracks;
	int m_NextId = 1;
	uint64_t m_LastTimeUs = 0;
	bool m_HasTime = false;
	cv::Size m_FrameSize;
};
