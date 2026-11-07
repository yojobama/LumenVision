// Replays the detections a RecordSink wrote to its telemetry sidecar (one JSON line per frame: frameNumber, captureTimeUs, producedTimeUs, json) through
// the tracker, with no model and no video. A recording made on the board therefore shows how the tracker would have done on it:
//   LUMEN_TRACK_REPLAY=<folder or .jsonl file> LumenCoreTests "[replay]"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ObjectTracker.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

namespace {

struct ReplayReport {
	int frames = 0;
	int detections = 0;
	int unconfirmed = 0;            // detections that never had an id (seen for fewer than minHits frames)
	std::set<int> ids;
	std::map<int, int> framesOfId;  // how many frames each id was seen in
	double seconds = 0.0;           // capture time covered
	double meanLifeFrames() const
	{
		if (framesOfId.empty()) return 0.0;
		double total = 0.0;
		for (const auto& [id, frames] : framesOfId) total += frames;
		return total / static_cast<double>(framesOfId.size());
	}
	double idsPerMinute() const { return seconds > 0.0 ? static_cast<double>(ids.size()) / seconds * 60.0 : 0.0; }
};

ReplayReport Replay(const std::filesystem::path& sidecar, const ObjectTrackingConfig& config)
{
	ReplayReport report;
	ObjectTracker tracker(config);
	std::ifstream file(sidecar);
	std::string line;
	uint64_t first = 0, last = 0;
	cv::Size frameSize(1, 1); // a frame without detections carries no size: the last one seen stands (the live sink always knows the real one)
	while (std::getline(file, line)) {
		if (line.empty()) continue;
		const nlohmann::json record = nlohmann::json::parse(line, nullptr, false);
		if (record.is_discarded() || !record.contains("captureTimeUs")) continue;
		const uint64_t captureUs = record["captureTimeUs"].get<uint64_t>();
		if (report.frames == 0) first = captureUs;
		last = captureUs;
		report.frames++;

		std::vector<ObjectDetection> detections;
		const nlohmann::json& entries = record.contains("json") ? record["json"] : nlohmann::json();
		if (entries.is_array()) {
			for (const auto& entry : entries) {
				if (!entry.contains("box")) continue;
				ObjectDetection detection;
				detection.SetClassId(entry.value("classId", 0));
				detection.SetClassName(entry.value("className", std::string()));
				detection.SetConfidence(entry.value("confidence", 0.0f));
				const auto& box = entry["box"];
				detection.SetBoundingBox(cv::Rect2d(box[0].get<double>(), box[1].get<double>(), box[2].get<double>(), box[3].get<double>()));
				detections.push_back(detection);
				frameSize = cv::Size(entry.value("frameWidth", frameSize.width), entry.value("frameHeight", frameSize.height));
			}
		}
		const auto info = tracker.Update(detections, captureUs, frameSize);
		for (const TrackInfo& track : info) {
			report.detections++;
			if (track.trackId < 0) { report.unconfirmed++; continue; }
			report.ids.insert(track.trackId);
			report.framesOfId[track.trackId]++;
		}
	}
	report.seconds = static_cast<double>(last - first) / 1e6;
	return report;
}

ObjectTrackingConfig Enabled()
{
	ObjectTrackingConfig config;
	config.enabled = true;
	return config;
}

}

TEST_CASE("a recorded sidecar replays through the tracker: two objects that cross keep two ids across a gap", "[tracker][replay]") {
	const auto report = Replay(std::string(LUMEN_TEST_DATA_DIR) + "/sidecar_two_balls.jsonl", Enabled());
	REQUIRE(report.frames == 45);
	CHECK(report.detections == 86); // two detections in 43 frames, none in the 2 that the recorder wrote as null
	CHECK(report.ids.size() == 2);
	CHECK(report.unconfirmed == 4); // each object's first two sightings, before an id is given
	CHECK(report.meanLifeFrames() > 38.0);
	CHECK(report.seconds == Catch::Approx(1.467).margin(0.01));
}

// Diagnostic, not part of the normal run: replays every sidecar in a folder (or one file) named by LUMEN_TRACK_REPLAY and prints how the tracker did.
TEST_CASE("replay of the recordings in LUMEN_TRACK_REPLAY", "[.][replay]") {
	const char* path = std::getenv("LUMEN_TRACK_REPLAY");
	REQUIRE(path != nullptr);
	std::vector<std::filesystem::path> files;
	if (std::filesystem::is_directory(path)) {
		for (const auto& entry : std::filesystem::directory_iterator(path))
			if (entry.path().extension() == ".jsonl") files.push_back(entry.path());
	} else {
		files.push_back(path);
	}
	REQUIRE_FALSE(files.empty());
	std::sort(files.begin(), files.end());
	for (const auto& file : files) {
		const auto report = Replay(file, Enabled());
		std::cout << file.filename().string() << ": " << report.frames << " frames over " << report.seconds << " s, " << report.detections << " detections, "
			<< report.ids.size() << " ids (" << report.idsPerMinute() << " a minute), mean track life " << report.meanLifeFrames() << " frames, "
			<< report.unconfirmed << " detections without an id\n";
		CHECK(report.frames > 0);
	}
}
