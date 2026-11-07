// Speed of ObjectTracker::Update against the number of detections in a frame. Hidden: timings only mean something in an optimised build, and the numbers
// are printed rather than asserted (the one assertion is a very loose ceiling, there to catch an accident like an O(n^3) blow-up):
//   LumenCoreTests "[.bench]"
#include <catch2/catch_test_macros.hpp>
#include "ObjectTracker.h"
#include <opencv2/core/optim.hpp>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>

namespace {

struct Mover { double x, y, vx, vy, w, h; int classId; };

struct Timing { double medianUs, p99Us; };

Timing Summarise(std::vector<double> us)
{
	std::sort(us.begin(), us.end());
	return { us[us.size() / 2], us[static_cast<size_t>(us.size() * 0.99)] };
}

}

TEST_CASE("tracker update time against detections per frame", "[.][bench]") {
	std::mt19937 rng(77);
	std::uniform_real_distribution<double> position(50, 1230), speed(-300, 300), size(25, 70);
	std::uniform_int_distribution<int> cls(0, 2);
	std::bernoulli_distribution churn(0.10);

	std::cout << "\ndetections  Update median / p99 (us)   assignment alone median / p99 (us)\n";
	double medianAtTwenty = 0.0;
	for (const int count : { 5, 20, 50, 100, 200 }) {
		ObjectTracker tracker([] { ObjectTrackingConfig config; config.enabled = true; return config; }());
		std::vector<Mover> movers;
		for (int i = 0; i < count; i++) movers.push_back({ position(rng), position(rng) * 0.5, speed(rng), speed(rng), size(rng), size(rng), cls(rng) });
		std::vector<double> updateUs, solveUs;
		const double fps = 120;
		for (int frame = 0; frame < 1200; frame++) {
			std::vector<ObjectDetection> detections;
			for (Mover& mover : movers) {
				mover.x += mover.vx / fps;
				mover.y += mover.vy / fps;
				if (mover.x < 20 || mover.x > 1260) mover.vx = -mover.vx;
				if (mover.y < 20 || mover.y > 700) mover.vy = -mover.vy;
				if (churn(rng) && frame % 10 == 0) continue; // a missed detection (about 1 frame in 100 per object)
				ObjectDetection detection;
				detection.SetClassId(mover.classId);
				detection.SetConfidence(0.8f);
				detection.SetClassName("o");
				detection.SetBoundingBox(cv::Rect2d(mover.x - mover.w / 2, mover.y - mover.h / 2, mover.w, mover.h));
				detections.push_back(detection);
			}
			const uint64_t timeUs = 1'000'000ull + static_cast<uint64_t>(frame * 1e6 / fps);
			const auto start = std::chrono::steady_clock::now();
			tracker.Update(detections, timeUs, cv::Size(1280, 720));
			const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
			if (frame >= 100) updateUs.push_back(us); // after the tracks have formed

			// the solver on a matrix of the same size (random costs, a third of the pairs forbidden), for what the matching itself costs
			if (frame >= 100 && frame % 4 == 0) {
				cv::Mat cost(static_cast<int>(tracker.TrackCount()), static_cast<int>(std::max<size_t>(detections.size(), 1)), CV_32F);
				std::uniform_real_distribution<float> unit(0.0f, 0.9f);
				for (int i = 0; i < cost.rows; i++)
					for (int j = 0; j < cost.cols; j++) cost.at<float>(i, j) = (i + j) % 3 == 0 ? std::numeric_limits<float>::infinity() : unit(rng);
				std::vector<int> assignment;
				const auto solveStart = std::chrono::steady_clock::now();
				if (cost.rows > 0) cv::linearAssignment(cost, assignment, 1.0);
				solveUs.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - solveStart).count());
			}
		}
		const Timing update = Summarise(updateUs), solve = Summarise(solveUs);
		std::cout << "  " << count << "          " << update.medianUs << " / " << update.p99Us << "                  " << solve.medianUs << " / " << solve.p99Us << "\n";
		if (count == 20) medianAtTwenty = update.medianUs;
		// even a heavily instrumented build must stay far below one 120 fps frame (8333 us) at 200 objects
		if (count == 200) CHECK(update.medianUs < 8000.0);
	}
	CHECK(medianAtTwenty < 2000.0);
}
