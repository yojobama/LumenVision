#include "ObjectTracker.h"
#include <opencv2/core/optim.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
	// alpha-beta filter gains: how much of the gap between the predicted and the measured centre is taken into the position and into the velocity
	constexpr double kPositionGain = 0.7;
	constexpr double kVelocityGain = 0.35;
	// how much of the gap in size is taken in
	constexpr double kSizeGain = 0.5;
}

ObjectTracker::ObjectTracker(ObjectTrackingConfig config)
{
	SetConfig(config);
}

void ObjectTracker::SetConfig(const ObjectTrackingConfig& config)
{
	m_Config = config;
	m_Config.minIoU = std::clamp(m_Config.minIoU, 0.001, 0.999);
	m_Config.minHits = std::max(1, m_Config.minHits);
	m_Config.maxAgeMs = std::max(1, m_Config.maxAgeMs);
}

void ObjectTracker::Reset()
{
	m_Tracks.clear();
	m_HasTime = false;
	m_LastTimeUs = 0;
	m_FrameSize = cv::Size();
}

double ObjectTracker::IoU(const cv::Rect2d& a, const cv::Rect2d& b)
{
	const double overlapW = std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x);
	const double overlapH = std::min(a.y + a.height, b.y + b.height) - std::max(a.y, b.y);
	if (!(overlapW > 0.0) || !(overlapH > 0.0)) return 0.0;
	const double intersection = overlapW * overlapH;
	const double unionArea = a.width * a.height + b.width * b.height - intersection;
	if (!(unionArea > 0.0) || !std::isfinite(unionArea)) return 0.0;
	return intersection / unionArea;
}

std::vector<TrackInfo> ObjectTracker::Update(const std::vector<ObjectDetection>& detections, uint64_t captureTimeUs, cv::Size frameSize)
{
	std::vector<TrackInfo> result(detections.size());
	const uint64_t maxAgeUs = static_cast<uint64_t>(m_Config.maxAgeMs) * 1000u;

	// the tracks belong to another picture, or are all older than a track may be
	if (m_HasTime && (frameSize != m_FrameSize || captureTimeUs < m_LastTimeUs || captureTimeUs - m_LastTimeUs > maxAgeUs)) m_Tracks.clear();
	const double dt = m_HasTime && captureTimeUs >= m_LastTimeUs ? static_cast<double>(captureTimeUs - m_LastTimeUs) / 1e6 : 0.0;
	m_HasTime = true;
	m_LastTimeUs = captureTimeUs;
	m_FrameSize = frameSize;

	// every track moves to where it should be now
	for (Track& track : m_Tracks) track.centre += track.velocity * dt;

	// optimal one-to-one matching on 1 - IoU; a pair that cannot go together (another class, too little overlap) is infinite, which the solver forbids
	std::vector<int> trackOfDetection(detections.size(), -1);
	if (!m_Tracks.empty() && !detections.empty()) {
		cv::Mat cost(static_cast<int>(m_Tracks.size()), static_cast<int>(detections.size()), CV_32F);
		for (int i = 0; i < cost.rows; i++) {
			const Track& track = m_Tracks[i];
			const cv::Rect2d predicted(track.centre.x - track.size.width / 2.0, track.centre.y - track.size.height / 2.0, track.size.width, track.size.height);
			for (int j = 0; j < cost.cols; j++) {
				float value = std::numeric_limits<float>::infinity();
				if (!m_Config.classAware || detections[j].GetClassId() == track.classId) {
					const double iou = IoU(predicted, detections[j].GetBoundingBox());
					if (iou >= m_Config.minIoU) value = static_cast<float>(1.0 - iou);
				}
				cost.at<float>(i, j) = value;
			}
		}
		// a pair costs less than 1 and leaving a track unmatched costs 1, so every allowed pair is made when it does not push another out
		std::vector<int> detectionOfTrack;
		cv::linearAssignment(cost, detectionOfTrack, 1.0);
		// a matrix with no usable cell may come back empty
		if (detectionOfTrack.size() == m_Tracks.size()) {
			for (size_t i = 0; i < detectionOfTrack.size(); i++) {
				const int j = detectionOfTrack[i];
				if (j >= 0 && j < static_cast<int>(detections.size())) trackOfDetection[j] = static_cast<int>(i);
			}
		}
	}

	std::vector<bool> trackMatched(m_Tracks.size(), false);
	std::vector<Track> next;
	next.reserve(m_Tracks.size() + detections.size());
	std::vector<int> nextOfDetection(detections.size(), -1);

	for (size_t j = 0; j < detections.size(); j++) {
		const cv::Rect2d box = detections[j].GetBoundingBox();
		const cv::Point2d measured(box.x + box.width / 2.0, box.y + box.height / 2.0);
		const int matched = trackOfDetection[j];
		Track track;
		if (matched >= 0) {
			trackMatched[matched] = true;
			track = m_Tracks[matched];
			const double sinceSeen = static_cast<double>(captureTimeUs - track.lastSeenUs) / 1e6;
			const cv::Point2d residual = measured - track.centre;
			if (sinceSeen > 0.0) {
				// the second sighting gives the velocity outright; after that the filter eases it towards what the residual says
				track.velocity = track.observations == 1 ? residual / sinceSeen : track.velocity + residual * (kVelocityGain / sinceSeen);
			}
			track.centre += residual * kPositionGain;
			track.size.width += (box.width - track.size.width) * kSizeGain;
			track.size.height += (box.height - track.size.height) * kSizeGain;
			track.observations++;
		} else {
			track.classId = detections[j].GetClassId();
			track.centre = measured;
			track.size = cv::Size2d(box.width, box.height);
			track.observations = 1;
		}
		track.lastSeenUs = captureTimeUs;
		if (track.publishedId < 0 && track.observations >= m_Config.minHits) track.publishedId = m_NextId++;
		nextOfDetection[j] = static_cast<int>(next.size());
		next.push_back(track);
	}

	// tracks nothing matched: a confirmed one coasts on its prediction until it has been unseen too long, an unconfirmed one is dropped at once
	for (size_t i = 0; i < m_Tracks.size(); i++) {
		if (trackMatched[i]) continue;
		const Track& track = m_Tracks[i];
		if (track.publishedId >= 0 && captureTimeUs - track.lastSeenUs <= maxAgeUs) next.push_back(track);
	}

	for (size_t j = 0; j < detections.size(); j++) {
		const Track& track = next[nextOfDetection[j]];
		result[j].trackId = track.publishedId;
		result[j].vxPxPerS = track.velocity.x;
		result[j].vyPxPerS = track.velocity.y;
	}
	m_Tracks = std::move(next);
	return result;
}
