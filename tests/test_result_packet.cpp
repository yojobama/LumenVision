#include <catch2/catch_test_macros.hpp>
#include "ResultPacket.h"
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

// The "result" packet is a cross-language contract: photoncompat's Java decoder reads the same golden file
// (tests/data/packet_v2.bin). Set LUMEN_UPDATE_GOLDEN=1 to rewrite it after an intentional layout change.

namespace {

PacketHeader GoldenHeader() {
	PacketHeader header;
	header.sequenceId = 42;
	header.latencyUs = 12345;
	header.multiTagIds = { 3, 7 };
	header.hasMultiTag = true;
	header.multiTagT = { 1.5, -2.25, 0.5 };
	header.multiTagQ = { 0.5, -0.5, 0.5, -0.5 };
	header.multiTagReprojErr = 0.75f;
	header.hasConstrained = true;
	header.constrainedX = 3.25;
	header.constrainedY = -1.5;
	header.constrainedYaw = 0.5;
	header.constrainedReprojErr = 0.25f;
	header.constrainedTagCount = 2;
	return header;
}

std::vector<PacketTarget> GoldenTargets() {
	PacketTarget tag;
	tag.fiducialId = 3;
	tag.yawDeg = 10.5;
	tag.pitchDeg = -4.25;
	tag.areaPercent = 1.75;
	tag.skewDeg = -12.5;
	tag.poseAmbiguity = 0.125;
	tag.hasPose = true;
	tag.bestT = { 0.5f, -0.25f, 2.0f };
	tag.bestQ = { 0.5f, 0.5f, 0.5f, 0.5f };
	tag.altT = { 0.75f, -0.5f, 2.5f };
	tag.altQ = { 1.0f, 0.0f, 0.0f, 0.0f };
	tag.bestReprojErr = 0.25f;
	tag.altReprojErr = 0.5f;
	tag.corners = { 100, 200, 300, 200, 300, 400, 100, 400 };
	tag.minAreaRectCorners = { 99, 199, 301, 199, 301, 401, 99, 401 };

	PacketTarget object;
	object.objectClassId = 5;
	object.objectConfidence = 0.875f;
	object.yawDeg = 3.0;
	object.pitchDeg = -1.0;
	object.areaPercent = 6.25;
	object.corners = { 10, 20, 110, 20, 110, 220, 10, 220 };
	object.minAreaRectCorners = object.corners;
	return { tag, object };
}

std::string GoldenPath() {
	return std::string(LUMEN_TEST_DATA_DIR) + "/packet_v2.bin";
}

}

TEST_CASE("a result packet survives a build and parse round trip", "[packet]") {
	PacketHeader header;
	std::vector<PacketTarget> targets;
	REQUIRE(ParseResultPacket(BuildResultPacket(GoldenHeader(), GoldenTargets()), header, targets));

	REQUIRE(header.sequenceId == 42);
	REQUIRE(header.latencyUs == 12345);
	REQUIRE(header.multiTagIds == std::vector<uint16_t>{ 3, 7 });
	REQUIRE(header.hasMultiTag);
	REQUIRE(header.multiTagT[1] == -2.25);
	REQUIRE(header.multiTagQ[3] == -0.5);
	REQUIRE(header.multiTagReprojErr == 0.75f);
	REQUIRE(header.hasConstrained);
	REQUIRE(header.constrainedX == 3.25);
	REQUIRE(header.constrainedY == -1.5);
	REQUIRE(header.constrainedYaw == 0.5);
	REQUIRE(header.constrainedReprojErr == 0.25f);
	REQUIRE(header.constrainedTagCount == 2);
	REQUIRE(targets.size() == 2);

	REQUIRE(targets[0].fiducialId == 3);
	REQUIRE(targets[0].objectClassId == -1);
	REQUIRE(targets[0].yawDeg == 10.5);
	REQUIRE(targets[0].skewDeg == -12.5);
	REQUIRE(targets[0].poseAmbiguity == 0.125);
	REQUIRE(targets[0].hasPose);
	REQUIRE(targets[0].bestT[2] == 2.0f);
	REQUIRE(targets[0].altT[1] == -0.5f);
	REQUIRE(targets[0].bestReprojErr == 0.25f);
	REQUIRE(targets[0].corners[6] == 100.0f);
	REQUIRE(targets[0].minAreaRectCorners[2] == 301.0f);

	REQUIRE(targets[1].fiducialId == -1);
	REQUIRE(targets[1].objectClassId == 5);
	REQUIRE(targets[1].objectConfidence == 0.875f);
	REQUIRE_FALSE(targets[1].hasPose);
	REQUIRE(targets[1].poseAmbiguity == -1.0);
}

TEST_CASE("a truncated or foreign-version result packet is rejected", "[packet]") {
	std::vector<uint8_t> packet = BuildResultPacket(GoldenHeader(), GoldenTargets());
	PacketHeader header;
	std::vector<PacketTarget> targets;

	std::vector<uint8_t> truncated(packet.begin(), packet.end() - 1);
	REQUIRE_FALSE(ParseResultPacket(truncated, header, targets));

	std::vector<uint8_t> foreign = packet;
	foreign[1] = 9; // schema version 9
	REQUIRE_FALSE(ParseResultPacket(foreign, header, targets));
	REQUIRE_FALSE(ParseResultPacket({}, header, targets));
}

TEST_CASE("the result packet layout matches the golden file the Java decoder reads", "[packet]") {
	std::vector<uint8_t> packet = BuildResultPacket(GoldenHeader(), GoldenTargets());

	if (std::getenv("LUMEN_UPDATE_GOLDEN") != nullptr) {
		std::ofstream out(GoldenPath(), std::ios::binary);
		out.write(reinterpret_cast<const char*>(packet.data()), static_cast<std::streamsize>(packet.size()));
	}

	std::ifstream in(GoldenPath(), std::ios::binary);
	REQUIRE(in.good());
	std::vector<uint8_t> golden((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	REQUIRE(golden == packet);
}
