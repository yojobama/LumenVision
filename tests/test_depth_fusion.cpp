#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "DepthFusionNode.h"

// A depth grid of 4 columns x 3 rows of 10x10 px blocks (metres; 0 = no depth), row by row: 1 2 3 0 / 0 5 5 5 / 9 9 9 9
namespace {
const std::vector<float> kDepth = { 1, 2, 3, 0, 0, 5, 5, 5, 9, 9, 9, 9 };
constexpr int kCols = 4, kRows = 3, kBlock = 10;
constexpr double kFx = 400.0, kCx = 20.0, kCy = 15.0;

nlohmann::json Fuse(const nlohmann::json& detections)
{
	return DepthFusionNode::FuseDetections(detections, kDepth, kCols, kRows, kBlock, kBlock, kFx, kCx, kCy);
}
}

TEST_CASE("object detections gain the median depth under their box, and their position at that range", "[depthfusion]") {
	// covers the first three blocks of the top row: depths 1, 2, 3
	nlohmann::json detections = nlohmann::json::array({ { {"className", "chair"}, {"box", { 0, 0, 30, 10 }} } });

	nlohmann::json fused = Fuse(detections);

	REQUIRE(fused.is_array());
	REQUIRE(fused.size() == 1);
	REQUIRE(fused[0]["className"] == "chair"); // what the detector said is kept
	REQUIRE(fused[0]["distanceMeters"] == Catch::Approx(2.0));
	REQUIRE(fused[0]["depthValidFraction"] == Catch::Approx(1.0));
	// box centre (15, 5) projected at 2 m: x = (15 - cx) * 2 / fx, y = (5 - cy) * 2 / fx
	REQUIRE(fused[0]["xMeters"] == Catch::Approx((15.0 - kCx) * 2.0 / kFx));
	REQUIRE(fused[0]["yMeters"] == Catch::Approx((5.0 - kCy) * 2.0 / kFx));
}

TEST_CASE("a box over blocks with no depth reports no range, not zero metres of position", "[depthfusion]") {
	nlohmann::json detections = nlohmann::json::array({ { {"box", { 30, 0, 10, 10 }} } }); // the one empty block of the top row

	nlohmann::json fused = Fuse(detections);

	REQUIRE(fused[0]["distanceMeters"] == 0.0);
	REQUIRE(fused[0]["depthValidFraction"] == 0.0);
	REQUIRE(fused[0]["xMeters"] == 0.0);
}

TEST_CASE("AprilTag detections are fused too, inside their own envelope", "[depthfusion]") {
	// a tag whose corners span x 10..30, y 10..20: the middle row, blocks 1..2 (depths 5, 5)
	nlohmann::json envelope = {
		{"tags", nlohmann::json::array({
			{ {"id", 42}, {"center", { 20, 15 }}, {"corners", { { 10, 20 }, { 30, 20 }, { 30, 10 }, { 10, 10 } }} },
			{ {"id", 7}, {"corners", { { 0, 20 }, { 40, 20 }, { 40, 30 }, { 0, 30 } }} },
		})},
		{"multiTag", nullptr},
		{"calibration", nullptr},
	};

	nlohmann::json fused = Fuse(envelope);

	REQUIRE(fused.is_object());
	REQUIRE(fused.contains("multiTag"));         // the rest of the envelope is untouched
	REQUIRE(fused["tags"].size() == 2);
	REQUIRE(fused["tags"][0]["id"] == 42);
	REQUIRE(fused["tags"][0]["distanceMeters"] == Catch::Approx(5.0));
	REQUIRE(fused["tags"][0]["xMeters"] == Catch::Approx((20.0 - kCx) * 5.0 / kFx));
	REQUIRE(fused["tags"][1]["distanceMeters"] == Catch::Approx(9.0)); // the bottom row
}

TEST_CASE("fusion leaves output it does not understand as it found it", "[depthfusion]") {
	REQUIRE(Fuse(nlohmann::json::object()) == nlohmann::json::object());
	REQUIRE(Fuse(nlohmann::json(5)) == nlohmann::json(5));
	// an array entry without a box is dropped, as before
	REQUIRE(Fuse(nlohmann::json::array({ { {"x", 1} } })).empty());
}
