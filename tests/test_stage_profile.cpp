#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "StageProfile.h"
#include "ApriltagDetector.h"
#include "ISource.h"
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp>

TEST_CASE("a stage reports its count, mean, percentiles and last value over a rolling window", "[profile]") {
	StageProfile profile;
	for (int i = 1; i <= 100; i++) profile.Record("work", i * 1000.0); // 1..100 ms

	auto stats = profile.Snapshot();
	REQUIRE(stats.size() == 1);
	REQUIRE(stats[0].name == "work");
	REQUIRE(stats[0].count == 100);
	REQUIRE(stats[0].meanUs == Catch::Approx(50500.0));
	REQUIRE(stats[0].p50Us == Catch::Approx(51000.0).margin(1000.0));
	REQUIRE(stats[0].p95Us == Catch::Approx(96000.0).margin(1000.0));
	REQUIRE(stats[0].maxUs == Catch::Approx(100000.0));
	REQUIRE(stats[0].lastUs == Catch::Approx(100000.0));
}

TEST_CASE("only the most recent window of samples counts towards the statistics", "[profile]") {
	StageProfile profile;
	for (size_t i = 0; i < StageProfile::kWindow; i++) profile.Record("s", 1000000.0); // an old slow phase
	for (size_t i = 0; i < StageProfile::kWindow; i++) profile.Record("s", 1000.0);    // then a fast one

	auto stats = profile.Snapshot();
	REQUIRE(stats[0].count == 2 * StageProfile::kWindow); // everything is counted...
	REQUIRE(stats[0].meanUs == Catch::Approx(1000.0));    // ...but the statistics describe the recent frames
	REQUIRE(stats[0].maxUs == Catch::Approx(1000.0));
}

TEST_CASE("stages are kept apart and a lap records the time since the previous mark", "[profile]") {
	StageProfile profile;
	{
		StageLap lap(&profile);
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		lap.Mark("first");
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		lap.Mark("second");
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
		lap.Skip(); // not counted
		lap.Mark("third");
	}
	{
		ScopedStage scope(&profile, "scoped");
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	std::map<std::string, double> meanMs;
	for (auto& s : profile.Snapshot()) meanMs[s.name] = s.meanUs / 1000.0;
	REQUIRE(meanMs.size() == 4);
	REQUIRE(meanMs["first"] >= 19.0);
	REQUIRE(meanMs["second"] >= 4.0);
	REQUIRE(meanMs["second"] < 25.0);   // sleeps overshoot by a timer tick (~15 ms on Windows)
	REQUIRE(meanMs["third"] < 25.0);   // the 30 ms before Skip() is not in it
	REQUIRE(meanMs["scoped"] >= 9.0);

	profile.Reset();
	REQUIRE(profile.Snapshot().empty());
}

TEST_CASE("both halves of a node share one profile, which goes away with the node", "[profile]") {
	std::shared_ptr<StageProfile> a = NodeProfiles::For("profile-test-node");
	std::shared_ptr<StageProfile> b = NodeProfiles::For("profile-test-node");
	REQUIRE(a.get() == b.get());
	a->Record("x", 2000.0);

	nlohmann::json all = nlohmann::json::parse(NodeProfiles::AllAsJson());
	REQUIRE(all.contains("profile-test-node"));
	REQUIRE(all["profile-test-node"][0]["stage"] == "x");
	REQUIRE(all["profile-test-node"][0]["meanMs"] == Catch::Approx(2.0));

	a.reset();
	b.reset();
	REQUIRE_FALSE(nlohmann::json::parse(NodeProfiles::AllAsJson()).contains("profile-test-node"));
}

namespace {
class StillSource : public ISource {
public:
	StillSource(std::shared_ptr<Logger> logger, std::string id, cv::Mat bgr) : ISource(logger, id), m_Bgr(bgr) {}
protected:
	void CaptureFrame() override {
		SetLatestResult(SourceResult(std::nullopt, m_Bgr));
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
private:
	cv::Mat m_Bgr;
};
}

TEST_CASE("a running source and detector record where their time goes", "[profile]") {
	auto logger = std::make_shared<Logger>("LumenCoreTests-profile.log");
	cv::Mat frame(240, 320, CV_8UC3, cv::Scalar(255, 255, 255));
	auto source = std::make_shared<StillSource>(logger, "profile-source", frame);
	CameraCalibrationResult calibration(700, 700, 160, 120, 0.1, {}, 320, 240);
	ApriltagDetector detector(logger, "profile-detector", calibration, 0.1651, APRILTAG_BACKEND_CPU);
	REQUIRE(detector.BindSource(source));

	source->Toggle(true);
	static_cast<ISink&>(detector).Toggle(true);
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	source->Toggle(false);
	static_cast<ISink&>(detector).Toggle(false);

	auto names = [](const std::string& id) {
		std::map<std::string, StageProfile::Stat> found;
		std::shared_ptr<StageProfile> profile = NodeProfiles::For(id);
		for (auto& s : profile->Snapshot()) found[s.name] = s;
		return found;
	};

	auto src = names("profile-source");
	REQUIRE(src.count("age") == 1);      // end-to-end age as published
	REQUIRE(src.count("interval") == 1); // time between results
	REQUIRE(src["interval"].meanUs > 5000.0);

	auto det = names("profile-detector");
	for (const char* stage : { "wait", "process", "gray", "detect", "tags+solve", "publish", "age" }) {
		INFO(stage);
		REQUIRE(det.count(stage) == 1);
		REQUIRE(det[stage].count > 5);
	}
	REQUIRE(det["process"].meanUs >= det["detect"].meanUs); // the whole is at least its biggest part
}
