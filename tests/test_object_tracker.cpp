#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ObjectTracker.h"
#include <opencv2/core/optim.hpp>
#include <cmath>
#include <limits>
#include <random>

namespace {

const cv::Size kFrame(1280, 720);
// the price of leaving a track unmatched in the assignment (a pair costs 1 - IoU, below this)
constexpr double kThreshold = 1.0;

ObjectDetection Detection(int classId, double cx, double cy, double w = 40, double h = 40)
{
	ObjectDetection detection;
	detection.SetClassId(classId);
	detection.SetClassName("ball");
	detection.SetConfidence(0.9f);
	detection.SetBoundingBox(cv::Rect2d(cx - w / 2.0, cy - h / 2.0, w, h));
	return detection;
}

uint64_t FrameTimeUs(int frame, double fps) { return 1'000'000ull + static_cast<uint64_t>(std::llround(frame * 1e6 / fps)); }

ObjectTrackingConfig Enabled()
{
	ObjectTrackingConfig config;
	config.enabled = true;
	return config;
}

// The simplest alternative to the optimal assignment: each track in turn takes the nearest unclaimed detection to where it was last seen
// (no motion prediction, no global view). It is here to show what the solver buys.
struct GreedyNearest {
	struct Track { double x, y; int id; };
	std::vector<Track> tracks;
	int next = 1;
	std::vector<int> Update(const std::vector<cv::Point2d>& centres)
	{
		std::vector<int> ids(centres.size(), -1);
		std::vector<bool> claimed(centres.size(), false);
		for (Track& track : tracks) {
			int best = -1;
			double bestDistance = 1e18;
			for (size_t j = 0; j < centres.size(); j++) {
				if (claimed[j]) continue;
				const double d = std::hypot(centres[j].x - track.x, centres[j].y - track.y);
				if (d < bestDistance) { bestDistance = d; best = static_cast<int>(j); }
			}
			if (best >= 0) { claimed[best] = true; ids[best] = track.id; track.x = centres[best].x; track.y = centres[best].y; }
		}
		for (size_t j = 0; j < centres.size(); j++)
			if (!claimed[j]) { ids[j] = next; tracks.push_back({ centres[j].x, centres[j].y, next++ }); }
		return ids;
	}
};

}

TEST_CASE("a steadily moving object keeps one id and reports its velocity", "[tracker]") {
	ObjectTracker tracker(Enabled());
	const double fps = 60, speed = 300; // pixels per second, to the right and down a little
	int firstId = -2;
	for (int frame = 0; frame < 40; frame++) {
		const double t = frame / fps;
		const auto info = tracker.Update({ Detection(0, 100 + speed * t, 200 + 0.2 * speed * t) }, FrameTimeUs(frame, fps), kFrame);
		REQUIRE(info.size() == 1);
		if (frame < 2) REQUIRE(info[0].trackId == -1); // unconfirmed until minHits (3) sightings
		else if (firstId == -2) firstId = info[0].trackId;
		else REQUIRE(info[0].trackId == firstId);
		if (frame >= 10) {
			CHECK(info[0].vxPxPerS == Catch::Approx(speed).epsilon(0.05));
			CHECK(info[0].vyPxPerS == Catch::Approx(0.2 * speed).epsilon(0.08));
		}
	}
	REQUIRE(firstId == 1);
}

TEST_CASE("an object confirms after minHits sightings, and a one-frame false positive takes no id", "[tracker]") {
	ObjectTrackingConfig config = Enabled();
	config.minHits = 1;
	ObjectTracker immediate(config);
	CHECK(immediate.Update({ Detection(0, 100, 100) }, FrameTimeUs(0, 30), kFrame)[0].trackId == 1);

	ObjectTracker tracker(Enabled());
	CHECK(tracker.Update({ Detection(0, 600, 300) }, FrameTimeUs(0, 30), kFrame)[0].trackId == -1); // a false positive, seen once
	// the real object, from the next frame on
	int id = -2;
	for (int frame = 1; frame < 6; frame++) id = tracker.Update({ Detection(0, 100 + frame * 3, 100) }, FrameTimeUs(frame, 30), kFrame)[0].trackId;
	CHECK(id == 1); // the false positive's brief life used no id
}

TEST_CASE("two identical objects that cross paths keep their ids where nearest-neighbour matching swaps them", "[tracker]") {
	ObjectTracker tracker(Enabled());
	GreedyNearest greedy;
	const double fps = 30;
	int idA = -2, idB = -2, greedyA = -2, greedyB = -2;
	bool greedySwapped = false;
	for (int frame = 0; frame < 21; frame++) {
		// A runs right, B runs left, 30 px a frame with 60 px boxes: they pass each other between frames 8 and 9, 6 px apart in y, so a step
		// from A's last place is shorter to B's new place than to A's own
		const cv::Point2d a(300 + 30.0 * frame, 300), b(785 - 30.0 * frame, 306);
		const auto info = tracker.Update({ Detection(0, a.x, a.y, 60, 60), Detection(0, b.x, b.y, 60, 60) }, FrameTimeUs(frame, fps), kFrame);
		const auto ids = greedy.Update({ a, b });
		if (frame == 4) { idA = info[0].trackId; idB = info[1].trackId; greedyA = ids[0]; greedyB = ids[1]; REQUIRE(idA > 0); REQUIRE(idB > 0); REQUIRE(idA != idB); }
		if (frame > 4) {
			REQUIRE(info[0].trackId == idA);
			REQUIRE(info[1].trackId == idB);
			if (ids[0] != greedyA || ids[1] != greedyB) greedySwapped = true;
		}
	}
	CHECK(greedySwapped);
}

TEST_CASE("a short disappearance keeps the id, a long one starts a new track", "[tracker]") {
	const double fps = 60;
	for (const int missing : { 5, 45 }) { // 83 ms (inside maxAgeMs = 500) and 750 ms
		ObjectTracker tracker(Enabled());
		int frame = 0;
		int id = -2;
		for (; frame < 10; frame++) id = tracker.Update({ Detection(0, 100 + 5.0 * frame, 200) }, FrameTimeUs(frame, fps), kFrame)[0].trackId;
		REQUIRE(id == 1);
		for (int k = 0; k < missing; k++, frame++) CHECK(tracker.Update({}, FrameTimeUs(frame, fps), kFrame).empty());
		// it reappears where its motion says it would be; two sightings later it has an id again
		int after = -2;
		for (int k = 0; k < 4; k++, frame++) after = tracker.Update({ Detection(0, 100 + 5.0 * frame, 200) }, FrameTimeUs(frame, fps), kFrame)[0].trackId;
		if (missing == 5) CHECK(after == 1);
		else CHECK(after == 2);
	}
}

TEST_CASE("a detection never continues a track of another class", "[tracker]") {
	ObjectTracker tracker(Enabled());
	int frame = 0;
	for (; frame < 5; frame++) tracker.Update({ Detection(0, 300, 300) }, FrameTimeUs(frame, 30), kFrame);
	const auto other = tracker.Update({ Detection(1, 300, 300) }, FrameTimeUs(frame++, 30), kFrame);
	CHECK(other[0].trackId == -1);
	// the original object is still tracked under its own id
	CHECK(tracker.Update({ Detection(0, 300, 300) }, FrameTimeUs(frame++, 30), kFrame)[0].trackId == 1);

	ObjectTrackingConfig loose = Enabled();
	loose.classAware = false;
	ObjectTracker anyClass(loose);
	for (int f = 0; f < 5; f++) anyClass.Update({ Detection(0, 300, 300) }, FrameTimeUs(f, 30), kFrame);
	CHECK(anyClass.Update({ Detection(1, 300, 300) }, FrameTimeUs(5, 30), kFrame)[0].trackId == 1);
}

TEST_CASE("the velocity is in pixels per second whatever the frame rate", "[tracker]") {
	for (const double fps : { 30.0, 60.0, 120.0 }) {
		ObjectTracker tracker(Enabled());
		TrackInfo last;
		for (int frame = 0; frame < 60; frame++) {
			const double t = frame / fps;
			last = tracker.Update({ Detection(0, 200 + 450.0 * t, 400 - 120.0 * t) }, FrameTimeUs(frame, fps), kFrame)[0];
		}
		INFO("fps " << fps);
		CHECK(last.vxPxPerS == Catch::Approx(450.0).epsilon(0.05));
		CHECK(last.vyPxPerS == Catch::Approx(-120.0).epsilon(0.08));
	}
}

TEST_CASE("a change of frame size or time running backwards starts over", "[tracker]") {
	ObjectTracker tracker(Enabled());
	int frame = 0;
	for (; frame < 6; frame++) tracker.Update({ Detection(0, 300, 300) }, FrameTimeUs(frame, 30), kFrame);
	CHECK(tracker.TrackCount() == 1);
	// another resolution: the old track is gone, the object is new (an id is never reused)
	int id = -2;
	for (int k = 0; k < 4; k++, frame++) id = tracker.Update({ Detection(0, 300, 300) }, FrameTimeUs(frame, 30), cv::Size(640, 360))[0].trackId;
	CHECK(id == 2);
	// a source restarted at an earlier capture time
	for (int k = 0; k < 4; k++) id = tracker.Update({ Detection(0, 300, 300) }, FrameTimeUs(k, 30), cv::Size(640, 360))[0].trackId;
	CHECK(id == 3);
}

TEST_CASE("empty, degenerate and duplicate input is handled", "[tracker]") {
	ObjectTracker tracker(Enabled());
	CHECK(tracker.Update({}, FrameTimeUs(0, 30), kFrame).empty());

	// zero-area and non-finite boxes get through without a crash or an id
	ObjectDetection nan = Detection(0, 10, 10);
	nan.SetBoundingBox(cv::Rect2d(std::numeric_limits<double>::quiet_NaN(), 5, 10, 10));
	ObjectDetection flat = Detection(0, 50, 50, 0, 0);
	for (int frame = 1; frame < 8; frame++) {
		const auto info = tracker.Update({ nan, flat }, FrameTimeUs(frame, 30), kFrame);
		REQUIRE(info.size() == 2);
	}

	// two identical boxes are two objects: one track each, never both on one
	ObjectTracker twins(Enabled());
	std::vector<TrackInfo> info;
	for (int frame = 0; frame < 6; frame++) info = twins.Update({ Detection(0, 400, 400), Detection(0, 400, 400) }, FrameTimeUs(frame, 30), kFrame);
	REQUIRE(info.size() == 2);
	CHECK(info[0].trackId > 0);
	CHECK(info[1].trackId > 0);
	CHECK(info[0].trackId != info[1].trackId);

	// the same capture time twice (a duplicated frame) neither divides by zero nor moves anything
	ObjectTracker same(Enabled());
	for (int frame = 0; frame < 5; frame++) same.Update({ Detection(0, 100, 100) }, FrameTimeUs(frame, 30), kFrame);
	const auto repeated = same.Update({ Detection(0, 100, 100) }, FrameTimeUs(4, 30), kFrame);
	CHECK(std::isfinite(repeated[0].vxPxPerS));
	CHECK(repeated[0].trackId == 1);
}

TEST_CASE("cv::linearAssignment agrees with an exhaustive search on small rectangular problems with forbidden pairs", "[tracker]") {
	std::mt19937 rng(12345);
	std::uniform_real_distribution<float> uniform(0.0f, 0.95f);
	std::bernoulli_distribution forbidden(0.35);

	// the optimum of sum(cost of matched pairs) + kThreshold * (unmatched among min(M, N)), over every partial matching that avoids forbidden cells
	struct Search {
		const cv::Mat& cost;
		double best = 1e18;
		void Run(int row, unsigned used, double total, int matched, int limit)
		{
			if (row == cost.rows) { best = std::min(best, total + kThreshold * (limit - matched)); return; }
			Run(row + 1, used, total, matched, limit); // row left unmatched
			for (int col = 0; col < cost.cols; col++) {
				const float c = cost.at<float>(row, col);
				if ((used >> col) & 1u || !std::isfinite(c)) continue;
				Run(row + 1, used | (1u << col), total + c, matched + 1, limit);
			}
		}
	};

	for (int trial = 0; trial < 300; trial++) {
		const int rows = 1 + static_cast<int>(rng() % 5), cols = 1 + static_cast<int>(rng() % 5);
		cv::Mat cost(rows, cols, CV_32F);
		for (int i = 0; i < rows; i++)
			for (int j = 0; j < cols; j++) cost.at<float>(i, j) = forbidden(rng) ? std::numeric_limits<float>::infinity() : uniform(rng);

		std::vector<int> assignment;
		cv::linearAssignment(cost, assignment, kThreshold);
		double total = 0.0;
		int matched = 0;
		if (assignment.size() == static_cast<size_t>(rows)) {
			std::vector<bool> usedColumn(cols, false);
			for (int i = 0; i < rows; i++) {
				if (assignment[i] < 0) continue;
				REQUIRE(assignment[i] < cols);
				REQUIRE_FALSE(usedColumn[assignment[i]]);
				usedColumn[assignment[i]] = true;
				REQUIRE(std::isfinite(cost.at<float>(i, assignment[i])));
				total += cost.at<float>(i, assignment[i]);
				matched++;
			}
		}
		const double solverObjective = total + kThreshold * (std::min(rows, cols) - matched);
		Search search{ cost };
		search.Run(0, 0u, 0.0, 0, std::min(rows, cols));
		INFO("trial " << trial << " " << rows << "x" << cols);
		REQUIRE(solverObjective == Catch::Approx(search.best).margin(1e-4));
	}
}
