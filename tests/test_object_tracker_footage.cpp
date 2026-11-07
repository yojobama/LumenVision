// ObjectDetectionSink with tracking, end to end on synthetic footage: rendered frames with known ground truth go through a real detector
// (one that finds each ball by its colour) and a real ObjectDetectionSink, and the track ids in its JSON are scored against the truth. The frames are handed to the
// sink one at a time and each is waited for, so no frame is dropped and the result does not depend on speed.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ObjectDetectionSink.h"
#include "IDetectionBackend.h"
#include "Logger.h"
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <thread>

namespace {

// a source that publishes exactly the frames it is given
class LockstepSource : public ISource {
public:
	LockstepSource(std::shared_ptr<Logger> logger, std::string id) : ISource(logger, id) {}
	void Publish(const cv::Mat& bgr, uint64_t captureUs)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Frame = bgr.clone();
		m_CaptureUs = captureUs;
		m_Pending = true;
		m_Wake.notify_all();
	}

protected:
	void CaptureFrame() override
	{
		std::unique_lock<std::mutex> lock(m_Mutex);
		if (!m_Wake.wait_for(lock, std::chrono::milliseconds(20), [&] { return m_Pending; })) return;
		m_Pending = false;
		SetLatestResult(SourceResult(std::nullopt, Frame(m_Frame), m_CaptureUs));
	}

private:
	std::mutex m_Mutex;
	std::condition_variable m_Wake;
	cv::Mat m_Frame;
	uint64_t m_CaptureUs = 0;
	bool m_Pending = false;
};

// Each ball has its own colour and the detector finds every colour separately, so two balls that overlap are still two boxes (the one in front
// hides part of the other, as it would for a network). White is the colour of the speckles that play false positives.
const std::vector<cv::Scalar>& Palette()
{
	static const std::vector<cv::Scalar> colours = { { 0, 0, 255 }, { 0, 255, 0 }, { 255, 0, 0 }, { 0, 255, 255 }, { 255, 0, 255 }, { 255, 255, 0 } };
	return colours;
}

class ColourBackend : public IDetectionBackend {
public:
	bool Load(const DetectionBackendConfig&) override { return true; }
	std::vector<ObjectDetection> Infer(const cv::Mat& bgr) override
	{
		std::vector<cv::Scalar> colours = Palette();
		colours.push_back(cv::Scalar(255, 255, 255));
		std::vector<ObjectDetection> out;
		for (const cv::Scalar& colour : colours) {
			cv::Mat mask, labels, stats, centroids;
			cv::inRange(bgr, colour - cv::Scalar(70, 70, 70), colour + cv::Scalar(70, 70, 70), mask);
			const int count = cv::connectedComponentsWithStats(mask, labels, stats, centroids);
			for (int i = 1; i < count; i++) {
				if (stats.at<int>(i, cv::CC_STAT_AREA) < 30) continue;
				ObjectDetection detection;
				detection.SetClassId(0);
				detection.SetClassName("ball");
				detection.SetConfidence(0.9f);
				detection.SetBoundingBox(cv::Rect2d(stats.at<int>(i, cv::CC_STAT_LEFT), stats.at<int>(i, cv::CC_STAT_TOP),
					stats.at<int>(i, cv::CC_STAT_WIDTH), stats.at<int>(i, cv::CC_STAT_HEIGHT)));
				out.push_back(detection);
			}
		}
		return out;
	}
	std::string Name() const override { return "colour"; }
};

struct Ball {
	cv::Point2d position;
	cv::Point2d velocity; // pixels per second
};

struct Scenario {
	cv::Size frame{ 320, 180 };
	double fps = 60;
	int frames = 120;
	double radius = 11;
	bool bounce = true;
	double missProbability = 0.0;  // a ball is not drawn in a frame (the detector misses it)
	double falsePositiveProbability = 0.0; // a speckle is drawn for one frame
	double barLeft = 0, barRight = 0; // a bar a ball is hidden behind while its centre is inside it (0 = none)
	double noise = 6.0;
	std::vector<Ball> balls;
	unsigned seed = 1;
};

struct Observation {
	int groundTruth; // which ball a detection is, -1 for a false positive
	int trackId;
	double vx, vy;
	int frame;
};

// runs a scenario through the sink and returns every detection with the ball it belongs to
std::vector<Observation> Run(Scenario scenario)
{
	auto logger = std::make_shared<Logger>("LumenCoreTests-object-tracker.log");
	auto source = std::make_shared<LockstepSource>(logger, "tracker-source");
	ObjectDetectionSink sink(logger, "tracker-sink", std::make_shared<ColourBackend>());
	ObjectTrackingConfig tracking;
	tracking.enabled = true;
	sink.SetTracking(tracking);
	REQUIRE(sink.BindSource(source));
	source->Toggle(true);
	static_cast<ISink&>(sink).Toggle(true);

	std::mt19937 rng(scenario.seed);
	std::uniform_real_distribution<double> unit(0.0, 1.0);
	std::normal_distribution<double> gauss(0.0, scenario.noise);
	std::vector<Observation> observations;
	const double dt = 1.0 / scenario.fps;
	const uint64_t start = 5'000'000ull;

	for (int frame = 0; frame < scenario.frames; frame++) {
		const uint64_t captureUs = start + static_cast<uint64_t>(std::llround(frame * 1e6 / scenario.fps));
		cv::Mat image(scenario.frame, CV_8UC3, cv::Scalar(20, 20, 20));
		std::vector<std::pair<int, cv::Point2d>> drawn;
		for (size_t b = 0; b < scenario.balls.size(); b++) {
			Ball& ball = scenario.balls[b];
			const bool hidden = scenario.barRight > scenario.barLeft && ball.position.x > scenario.barLeft && ball.position.x < scenario.barRight;
			if (!hidden && unit(rng) >= scenario.missProbability) {
				cv::circle(image, cv::Point(cvRound(ball.position.x), cvRound(ball.position.y)), cvRound(scenario.radius), Palette()[b % Palette().size()], cv::FILLED, cv::LINE_8);
				drawn.emplace_back(static_cast<int>(b), ball.position);
			}
			ball.position += ball.velocity * dt;
			if (scenario.bounce) {
				if (ball.position.x < scenario.radius || ball.position.x > scenario.frame.width - scenario.radius) ball.velocity.x = -ball.velocity.x;
				if (ball.position.y < scenario.radius || ball.position.y > scenario.frame.height - scenario.radius) ball.velocity.y = -ball.velocity.y;
			}
		}
		if (unit(rng) < scenario.falsePositiveProbability)
			cv::rectangle(image, cv::Rect(static_cast<int>(unit(rng) * (scenario.frame.width - 10)), static_cast<int>(unit(rng) * (scenario.frame.height - 10)), 8, 8),
				cv::Scalar(255, 255, 255), cv::FILLED);
		cv::Mat noise(image.size(), CV_32FC3);
		cv::randn(noise, 0.0, scenario.noise);
		cv::Mat noisy;
		image.convertTo(noisy, CV_32FC3);
		noisy += noise;
		noisy.convertTo(image, CV_8UC3);

		source->Publish(image, captureUs);
		SourceResult result;
		for (int wait = 0; wait < 2000; wait++) {
			result = sink.GetLatestResult();
			if (result.captureTimeUs == captureUs && result.json.has_value()) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		REQUIRE(result.captureTimeUs == captureUs);
		for (const auto& entry : *result.json) {
			const auto& box = entry["box"];
			const cv::Point2d centre(box[0].get<double>() + box[2].get<double>() / 2.0, box[1].get<double>() + box[3].get<double>() / 2.0);
			int truth = -1;
			double nearest = 12.0; // a box centred within 12 px of a drawn ball is that ball
			for (const auto& [id, position] : drawn) {
				const double d = std::hypot(position.x - centre.x, position.y - centre.y);
				if (d < nearest) { nearest = d; truth = id; }
			}
			observations.push_back({ truth, entry["trackId"].get<int>(), entry["vxPxPerS"].get<double>(), entry["vyPxPerS"].get<double>(), frame });
		}
	}
	static_cast<ISink&>(sink).Toggle(false);
	source->Toggle(false);
	return observations;
}

struct Score {
	int idSwitches = 0;       // a ball's track id changing from one confirmed sighting to the next
	int impureTracks = 0;     // a track id that was on more than one ball
	std::map<int, std::set<int>> idsOfBall;
};

Score Evaluate(const std::vector<Observation>& observations)
{
	Score score;
	std::map<int, int> lastId; // ball -> its last confirmed id
	std::map<int, std::set<int>> ballsOfId;
	for (const Observation& o : observations) {
		if (o.groundTruth < 0 || o.trackId < 0) continue;
		score.idsOfBall[o.groundTruth].insert(o.trackId);
		ballsOfId[o.trackId].insert(o.groundTruth);
		auto last = lastId.find(o.groundTruth);
		if (last != lastId.end() && last->second != o.trackId) score.idSwitches++;
		lastId[o.groundTruth] = o.trackId;
	}
	for (const auto& [id, balls] : ballsOfId)
		if (balls.size() > 1) score.impureTracks++;
	return score;
}

}

TEST_CASE("two balls that cross keep their ids through the whole sink", "[tracker][footage]") {
	Scenario scenario;
	scenario.bounce = false;
	scenario.frames = 100;
	scenario.balls = { { { 40, 80 }, { 200, 0 } }, { { 280, 86 }, { -200, 0 } } }; // meet at about frame 36, 6 px apart in y
	scenario.seed = 7;
	const auto observations = Run(scenario);
	const Score score = Evaluate(observations);
	INFO("id switches " << score.idSwitches << ", impure tracks " << score.impureTracks);
	REQUIRE(score.idsOfBall.size() == 2);
	CHECK(score.idsOfBall.at(0).size() == 1);
	CHECK(score.idsOfBall.at(1).size() == 1);
	CHECK(score.idSwitches == 0);
	CHECK(score.impureTracks == 0);
}

TEST_CASE("a ball hidden behind a bar for a moment is the same track when it comes out", "[tracker][footage]") {
	Scenario scenario;
	scenario.bounce = false;
	scenario.frames = 100;
	scenario.barLeft = 150;
	scenario.barRight = 175; // about 0.2 s at 180 px/s, well inside the 0.5 s a track survives
	scenario.balls = { { { 30, 90 }, { 180, 0 } } };
	scenario.seed = 3;
	const Score score = Evaluate(Run(scenario));
	REQUIRE(score.idsOfBall.count(0) == 1);
	CHECK(score.idsOfBall.at(0).size() == 1);
	CHECK(score.idSwitches == 0);
}

TEST_CASE("six bouncing balls with missed detections and false positives keep their identities", "[tracker][footage]") {
	Scenario scenario;
	scenario.frames = 300;
	scenario.missProbability = 0.05;
	scenario.falsePositiveProbability = 0.05;
	scenario.radius = 9;
	std::mt19937 rng(21);
	std::uniform_real_distribution<double> x(30, 290), y(25, 155), speed(-150, 150);
	for (int i = 0; i < 6; i++) scenario.balls.push_back({ { x(rng), y(rng) }, { speed(rng) + 60, speed(rng) } });
	scenario.seed = 11;
	const auto observations = Run(scenario);
	const Score score = Evaluate(observations);
	int falsePositivesConfirmed = 0;
	for (const Observation& o : observations)
		if (o.groundTruth < 0 && o.trackId > 0) falsePositivesConfirmed++;
	INFO("id switches " << score.idSwitches << ", impure tracks " << score.impureTracks << ", confirmed false positives " << falsePositivesConfirmed);
	// balls that touch merge into one blob, and a missed detection is a frame without the ball: a few switches are the cost of that, many are a fault
	CHECK(score.idSwitches <= 5);
	CHECK(score.impureTracks <= 3);
	CHECK(falsePositivesConfirmed == 0); // a one-frame speckle never reaches minHits
}

TEST_CASE("the velocity reported is the ball's, in pixels per second, at 30, 60 and 120 fps", "[tracker][footage]") {
	for (const double fps : { 30.0, 60.0, 120.0 }) {
		Scenario scenario;
		scenario.fps = fps;
		scenario.bounce = false;
		scenario.frames = static_cast<int>(fps * 0.8);
		scenario.balls = { { { 30, 70 }, { 240, 60 } } };
		scenario.seed = 5;
		const auto observations = Run(scenario);
		REQUIRE(observations.size() > 10);
		double vx = 0, vy = 0;
		int counted = 0;
		for (const Observation& o : observations)
			if (o.frame >= scenario.frames / 2 && o.groundTruth == 0) { vx += o.vx; vy += o.vy; counted++; }
		REQUIRE(counted > 0);
		INFO("fps " << fps << " mean velocity " << vx / counted << ", " << vy / counted);
		CHECK(vx / counted == Catch::Approx(240.0).epsilon(0.12));
		CHECK(vy / counted == Catch::Approx(60.0).epsilon(0.25));
	}
}

TEST_CASE("with tracking off the sink's output has no track fields", "[tracker][footage]") {
	auto logger = std::make_shared<Logger>("LumenCoreTests-object-tracker.log");
	auto source = std::make_shared<LockstepSource>(logger, "off-source");
	ObjectDetectionSink sink(logger, "off-sink", std::make_shared<ColourBackend>());
	REQUIRE_FALSE(sink.GetTracking().enabled);
	REQUIRE(sink.BindSource(source));
	source->Toggle(true);
	static_cast<ISink&>(sink).Toggle(true);
	cv::Mat image(180, 320, CV_8UC3, cv::Scalar(20, 20, 20));
	cv::circle(image, cv::Point(100, 90), 11, cv::Scalar(255, 255, 255), cv::FILLED);
	source->Publish(image, 9'000'000ull);
	SourceResult result;
	for (int wait = 0; wait < 2000 && result.captureTimeUs != 9'000'000ull; wait++) {
		result = sink.GetLatestResult();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	REQUIRE(result.json.has_value());
	REQUIRE(result.json->size() == 1);
	CHECK_FALSE((*result.json)[0].contains("trackId"));
	CHECK_FALSE((*result.json)[0].contains("vxPxPerS"));
	static_cast<ISink&>(sink).Toggle(false);
	source->Toggle(false);
}

#ifdef LUMEN_WITH_ONNX
#include "OnnxDetectionBackend.h"
#include <opencv2/imgcodecs.hpp>

// A real network on real pixels: bus.jpg panned sideways two pixels a frame (as a camera turning), through YOLOv8n and the sink with tracking on.
TEST_CASE("the bus and the people in a panning photo keep their ids with a real YOLO model", "[tracker][footage][onnx]") {
	DetectionBackendConfig config;
	config.modelPath = std::string(LUMEN_TEST_DATA_DIR) + "/yolov8n.onnx";
	config.labelsPath = std::string(LUMEN_TEST_DATA_DIR) + "/coco_80_labels.txt";
	config.variant = YOLOv8;
	config.confThreshold = 0.5f;
	config.nmsThreshold = 0.45f;
	auto backend = std::make_shared<OnnxDetectionBackend>();
	REQUIRE(backend->Load(config));

	const cv::Mat photo = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");
	REQUIRE_FALSE(photo.empty());
	const int width = photo.cols - 100;

	auto logger = std::make_shared<Logger>("LumenCoreTests-object-tracker.log");
	auto source = std::make_shared<LockstepSource>(logger, "bus-source");
	ObjectDetectionSink sink(logger, "bus-sink", backend);
	ObjectTrackingConfig tracking;
	tracking.enabled = true;
	sink.SetTracking(tracking);
	REQUIRE(sink.BindSource(source));
	source->Toggle(true);
	static_cast<ISink&>(sink).Toggle(true);

	std::set<int> busIds, personIds;
	int busFrames = 0, maxPeople = 0;
	for (int frame = 0; frame < 50; frame++) {
		const uint64_t captureUs = 7'000'000ull + static_cast<uint64_t>(frame) * 33'333ull;
		source->Publish(photo(cv::Rect(2 * frame, 0, width, photo.rows)), captureUs);
		SourceResult result;
		for (int wait = 0; wait < 5000 && !(result.captureTimeUs == captureUs && result.json.has_value()); wait++) {
			result = sink.GetLatestResult();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		REQUIRE(result.captureTimeUs == captureUs);
		int people = 0;
		for (const auto& entry : *result.json) {
			const int id = entry["trackId"].get<int>();
			if (entry["className"] == "person") { people++; if (id > 0) personIds.insert(id); }
			if (entry["className"] == "bus") { busFrames++; if (id > 0) busIds.insert(id); }
		}
		maxPeople = std::max(maxPeople, people);
	}
	static_cast<ISink&>(sink).Toggle(false);
	source->Toggle(false);

	INFO("bus ids " << busIds.size() << " in " << busFrames << " frames; person ids " << personIds.size() << ", at most " << maxPeople << " people in a frame");
	REQUIRE(busFrames >= 45);
	CHECK(busIds.size() == 1);
	CHECK(maxPeople >= 3);
	// every person keeps one id; a detection that comes and goes at the edge of the confidence cutoff may add one
	CHECK(personIds.size() <= static_cast<size_t>(maxPeople) + 1);
}
#endif
