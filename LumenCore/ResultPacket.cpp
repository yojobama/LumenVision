#include "ResultPacket.h"
#include <algorithm>
#include <cstring>

namespace {
	void AppendU8(std::vector<uint8_t>& buf, uint8_t v) {
		buf.push_back(v);
	}
	void AppendU16(std::vector<uint8_t>& buf, uint16_t v) {
		buf.push_back(static_cast<uint8_t>(v >> 8));
		buf.push_back(static_cast<uint8_t>(v));
	}
	void AppendU32(std::vector<uint8_t>& buf, uint32_t v) {
		for (int shift = 24; shift >= 0; shift -= 8) buf.push_back(static_cast<uint8_t>(v >> shift));
	}
	void AppendU64(std::vector<uint8_t>& buf, uint64_t v) {
		for (int shift = 56; shift >= 0; shift -= 8) buf.push_back(static_cast<uint8_t>(v >> shift));
	}
	void AppendF32(std::vector<uint8_t>& buf, float v) {
		uint32_t bits;
		static_assert(sizeof(bits) == sizeof(v));
		std::memcpy(&bits, &v, sizeof(bits));
		AppendU32(buf, bits);
	}
	void AppendF64(std::vector<uint8_t>& buf, double v) {
		uint64_t bits;
		static_assert(sizeof(bits) == sizeof(v));
		std::memcpy(&bits, &v, sizeof(bits));
		AppendU64(buf, bits);
	}

	class Reader {
	public:
		explicit Reader(const std::vector<uint8_t>& bytes) : m_Bytes(bytes) {}
		bool ok() const { return m_Ok; }

		uint8_t U8() { return static_cast<uint8_t>(Read(1)); }
		uint16_t U16() { return static_cast<uint16_t>(Read(2)); }
		uint32_t U32() { return static_cast<uint32_t>(Read(4)); }
		uint64_t U64() { return Read(8); }
		float F32() {
			uint32_t bits = U32();
			float v;
			std::memcpy(&v, &bits, sizeof(v));
			return v;
		}
		double F64() {
			uint64_t bits = U64();
			double v;
			std::memcpy(&v, &bits, sizeof(v));
			return v;
		}

	private:
		uint64_t Read(size_t count) {
			if (!m_Ok || m_Offset + count > m_Bytes.size()) {
				m_Ok = false;
				return 0;
			}
			uint64_t v = 0;
			for (size_t i = 0; i < count; i++) v = (v << 8) | m_Bytes[m_Offset + i];
			m_Offset += count;
			return v;
		}

		const std::vector<uint8_t>& m_Bytes;
		size_t m_Offset = 0;
		bool m_Ok = true;
	};
}

std::vector<uint8_t> BuildResultPacket(const PacketHeader& header, const std::vector<PacketTarget>& targets)
{
	std::vector<uint8_t> packet;
	AppendU16(packet, RESULT_PACKET_SCHEMA_VERSION);
	AppendU64(packet, header.sequenceId);
	AppendU32(packet, header.latencyUs);

	AppendU8(packet, header.hasMultiTag ? 1 : 0);
	if (header.hasMultiTag) {
		for (double v : header.multiTagT) AppendF64(packet, v);
		for (double v : header.multiTagQ) AppendF64(packet, v);
		AppendF32(packet, header.multiTagReprojErr);
	}

	AppendU8(packet, header.hasConstrained ? 1 : 0);
	if (header.hasConstrained) {
		AppendF64(packet, header.constrainedX);
		AppendF64(packet, header.constrainedY);
		AppendF64(packet, header.constrainedYaw);
		AppendF32(packet, header.constrainedReprojErr);
		AppendU8(packet, header.constrainedTagCount);
	}

	size_t idCount = std::min<size_t>(header.multiTagIds.size(), 255);
	AppendU8(packet, static_cast<uint8_t>(idCount));
	for (size_t i = 0; i < idCount; i++) AppendU16(packet, header.multiTagIds[i]);

	size_t targetCount = std::min<size_t>(targets.size(), 65535);
	AppendU16(packet, static_cast<uint16_t>(targetCount));
	for (size_t i = 0; i < targetCount; i++) {
		const PacketTarget& t = targets[i];
		AppendU16(packet, static_cast<uint16_t>(t.fiducialId));
		AppendU16(packet, static_cast<uint16_t>(t.objectClassId));
		AppendF32(packet, t.objectConfidence);
		AppendF64(packet, t.yawDeg);
		AppendF64(packet, t.pitchDeg);
		AppendF64(packet, t.areaPercent);
		AppendF64(packet, t.skewDeg);
		AppendF64(packet, t.poseAmbiguity);
		for (float v : t.bestT) AppendF32(packet, v);
		for (float v : t.bestQ) AppendF32(packet, v);
		for (float v : t.altT) AppendF32(packet, v);
		for (float v : t.altQ) AppendF32(packet, v);
		AppendF32(packet, t.bestReprojErr);
		AppendF32(packet, t.altReprojErr);
		for (float v : t.corners) AppendF32(packet, v);
		for (float v : t.minAreaRectCorners) AppendF32(packet, v);
	}
	return packet;
}

bool ParseResultPacket(const std::vector<uint8_t>& bytes, PacketHeader& header, std::vector<PacketTarget>& targets)
{
	Reader in(bytes);
	if (in.U16() != RESULT_PACKET_SCHEMA_VERSION) return false;
	header = PacketHeader();
	header.sequenceId = in.U64();
	header.latencyUs = in.U32();
	header.hasMultiTag = in.U8() != 0;
	if (header.hasMultiTag) {
		for (double& v : header.multiTagT) v = in.F64();
		for (double& v : header.multiTagQ) v = in.F64();
		header.multiTagReprojErr = in.F32();
	}
	header.hasConstrained = in.U8() != 0;
	if (header.hasConstrained) {
		header.constrainedX = in.F64();
		header.constrainedY = in.F64();
		header.constrainedYaw = in.F64();
		header.constrainedReprojErr = in.F32();
		header.constrainedTagCount = in.U8();
	}
	uint8_t idCount = in.U8();
	for (uint8_t i = 0; i < idCount; i++) header.multiTagIds.push_back(in.U16());

	uint16_t targetCount = in.U16();
	targets.clear();
	for (uint16_t i = 0; i < targetCount && in.ok(); i++) {
		PacketTarget t;
		t.fiducialId = static_cast<int16_t>(in.U16());
		t.objectClassId = static_cast<int16_t>(in.U16());
		t.objectConfidence = in.F32();
		t.yawDeg = in.F64();
		t.pitchDeg = in.F64();
		t.areaPercent = in.F64();
		t.skewDeg = in.F64();
		t.poseAmbiguity = in.F64();
		for (float& v : t.bestT) v = in.F32();
		for (float& v : t.bestQ) v = in.F32();
		for (float& v : t.altT) v = in.F32();
		for (float& v : t.altQ) v = in.F32();
		t.bestReprojErr = in.F32();
		t.altReprojErr = in.F32();
		for (float& v : t.corners) v = in.F32();
		for (float& v : t.minAreaRectCorners) v = in.F32();
		t.hasPose = t.bestReprojErr >= 0.0f;
		targets.push_back(t);
	}
	return in.ok();
}
