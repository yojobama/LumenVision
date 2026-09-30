#pragma once
#include <array>
#include <cstdint>
#include <vector>

// The binary per-camera "result" packet NetworkTablesSink publishes, schema version 2. All integers and floats are big-endian:
//
//   header:  u16 schemaVersion, u64 sequenceId, u32 latencyUs,
//            u8 hasMultiTag, [if 1: f64 t[3], f64 q[4](w,x,y,z), f32 reprojErr],
//            u8 multiTagIdCount, u16 multiTagIds[multiTagIdCount], u16 targetCount
//   target:  i16 fiducialId, i16 objectClassId, f32 objectConfidence,
//            f64 yaw, f64 pitch, f64 area, f64 skew, f64 poseAmbiguity,
//            f32 bestT[3], f32 bestQ[4](w,x,y,z), f32 altT[3], f32 altQ[4](w,x,y,z), f32 bestReprojErr, f32 altReprojErr,
//            f32 corners[8](x0,y0,..,x3,y3), f32 minAreaRectCorners[8]
//
// Every pose is in WPILib frames: a target's transforms are camera-to-tag (camera X forward / Y left / Z up, tag frame as in a field
// layout) and the multi-tag pose is the camera's pose in the field in the same camera axes. yaw is positive to the left, pitch up.
//
// fiducialId / objectClassId are -1 and objectConfidence / poseAmbiguity / the reprojection errors are -1 when not applicable. A target
// with no pose has zero translations and identity quaternions. The Java decoder in photoncompat follows this comment.
struct PacketTarget {
	int16_t fiducialId = -1;
	int16_t objectClassId = -1;
	float objectConfidence = -1.0f;
	double yawDeg = 0.0;
	double pitchDeg = 0.0;
	double areaPercent = 0.0;
	double skewDeg = 0.0;
	double poseAmbiguity = -1.0;
	bool hasPose = false;
	std::array<float, 3> bestT{};
	std::array<float, 4> bestQ{ 1.0f, 0.0f, 0.0f, 0.0f };
	std::array<float, 3> altT{};
	std::array<float, 4> altQ{ 1.0f, 0.0f, 0.0f, 0.0f };
	float bestReprojErr = -1.0f;
	float altReprojErr = -1.0f;
	std::array<float, 8> corners{};
	std::array<float, 8> minAreaRectCorners{};
};

struct PacketHeader {
	uint64_t sequenceId = 0;
	// capture to publish, in microseconds
	uint32_t latencyUs = 0;
	// the coprocessor's multi-tag solve: the camera's pose in the field, and the tag ids it used
	bool hasMultiTag = false;
	std::array<double, 3> multiTagT{};
	std::array<double, 4> multiTagQ{ 1.0, 0.0, 0.0, 0.0 };
	float multiTagReprojErr = -1.0f;
	std::vector<uint16_t> multiTagIds;
};

inline constexpr uint16_t RESULT_PACKET_SCHEMA_VERSION = 2;

std::vector<uint8_t> BuildResultPacket(const PacketHeader& header, const std::vector<PacketTarget>& targets);

// the inverse of BuildResultPacket, for tests; false on a truncated packet or another schema version
bool ParseResultPacket(const std::vector<uint8_t>& bytes, PacketHeader& header, std::vector<PacketTarget>& targets);
