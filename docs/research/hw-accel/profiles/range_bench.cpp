// Full-range to video-range plane conversion as the encoders do it today (a 256-entry table, one load per byte), against a NEON version of the same table
// (tbl instructions, 16 bytes at a time) and a NEON arithmetic version. Exactness first (all 256 inputs), then speed at the real frame sizes.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

static inline uint8_t SatRound(double v) { long r = std::lrint(v); return static_cast<uint8_t>(r < 0 ? 0 : (r > 255 ? 255 : r)); }

static uint8_t g_luma[256], g_chroma[256];

static void TableScalar(const uint8_t* __restrict src, uint8_t* __restrict dst, size_t n, const uint8_t* table)
{
	for (size_t i = 0; i < n; i++) dst[i] = table[src[i]];
}

#if defined(__aarch64__)
// The same table, 16 bytes at a time: four chained 64-byte table lookups (vqtbl4q returns 0 for an out-of-range index, so the partial results OR together).
static void TableTbl(const uint8_t* __restrict src, uint8_t* __restrict dst, size_t n, const uint8_t* table)
{
	const uint8x16x4_t t0 = vld1q_u8_x4(table), t1 = vld1q_u8_x4(table + 64), t2 = vld1q_u8_x4(table + 128), t3 = vld1q_u8_x4(table + 192);
	const uint8x16_t k64 = vdupq_n_u8(64), k128 = vdupq_n_u8(128), k192 = vdupq_n_u8(192);
	size_t i = 0;
	for (; i + 16 <= n; i += 16) {
		const uint8x16_t v = vld1q_u8(src + i);
		uint8x16_t r = vqtbl4q_u8(t0, v);
		r = vorrq_u8(r, vqtbl4q_u8(t1, vsubq_u8(v, k64)));
		r = vorrq_u8(r, vqtbl4q_u8(t2, vsubq_u8(v, k128)));
		r = vorrq_u8(r, vqtbl4q_u8(t3, vsubq_u8(v, k192)));
		vst1q_u8(dst + i, r);
	}
	for (; i < n; i++) dst[i] = table[src[i]];
}

// luma = 16 + (146 v + 85) / 170 and chroma = (448 v + 8191) / 510 with the divisions as multiply-shifts, exact for the ranges used (checked in main)
template <uint32_t Mul, uint32_t Add, uint32_t Gain, uint16_t Offset>
static void Arith(const uint8_t* __restrict src, uint8_t* __restrict dst, size_t n)
{
	size_t i = 0;
	for (; i + 16 <= n; i += 16) {
		const uint8x16_t v = vld1q_u8(src + i);
		const uint16x8_t lo = vmovl_u8(vget_low_u8(v)), hi = vmovl_u8(vget_high_u8(v));
		auto part = [](uint16x8_t x) {
			uint32x4_t t0 = vmlal_n_u16(vdupq_n_u32(Add), vget_low_u16(x), static_cast<uint16_t>(Gain));
			uint32x4_t t1 = vmlal_n_u16(vdupq_n_u32(Add), vget_high_u16(x), static_cast<uint16_t>(Gain));
			uint32x4_t q0 = vshrq_n_u32(vmulq_n_u32(t0, Mul), 22), q1 = vshrq_n_u32(vmulq_n_u32(t1, Mul), 22);
			return vaddq_u16(vcombine_u16(vmovn_u32(q0), vmovn_u32(q1)), vdupq_n_u16(Offset));
		};
		vst1q_u8(dst + i, vcombine_u8(vmovn_u16(part(lo)), vmovn_u16(part(hi))));
	}
}
#endif

template <class F> static double TimeMs(F f, int reps)
{
	f();
	const auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < reps; i++) {
		f();
		asm volatile("" : : : "memory");
	}
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps;
}

int main()
{
	for (int v = 0; v < 256; v++) {
		g_luma[v] = SatRound(16.0 + v * 219.0 / 255.0);
		g_chroma[v] = SatRound(128.0 + (v - 128) * 224.0 / 255.0);
	}
	uint8_t in[256];
	for (int v = 0; v < 256; v++) in[v] = static_cast<uint8_t>(v);
#if defined(__aarch64__)
	{
		uint8_t out[256];
		int bad = 0;
		TableTbl(in, out, 256, g_luma);
		for (int v = 0; v < 256; v++) bad += out[v] != g_luma[v];
		TableTbl(in, out, 256, g_chroma);
		for (int v = 0; v < 256; v++) bad += out[v] != g_chroma[v];
		std::printf("tbl mismatches over both tables: %d\n", bad);
		bad = 0;
		Arith<24673, 85, 146, 16>(in, out, 256);
		for (int v = 0; v < 256; v++) bad += out[v] != g_luma[v];
		std::printf("arith luma mismatches: %d\n", bad);
		bad = 0;
		Arith<8224, 8191, 448, 0>(in, out, 256);
		for (int v = 0; v < 256; v++) bad += out[v] != g_chroma[v];
		std::printf("arith chroma mismatches: %d\n", bad);
	}
#endif
	struct Size { const char* name; size_t luma; } sizes[] = { { "1280x720", 1280u * 720 }, { "2560x720", 2560u * 720 } };
	for (const Size& s : sizes) {
		std::vector<uint8_t> src(s.luma), dst(s.luma);
		for (size_t i = 0; i < s.luma; i++) src[i] = static_cast<uint8_t>(i * 2654435761u >> 24);
		const size_t chromaBytes = s.luma / 2;
		std::vector<uint8_t> csrc(chromaBytes), cdst(chromaBytes);
		for (size_t i = 0; i < chromaBytes; i++) csrc[i] = static_cast<uint8_t>(i * 40503u >> 8);
		const int reps = 200;
		const double scalarL = TimeMs([&] { TableScalar(src.data(), dst.data(), s.luma, g_luma); }, reps);
		const double scalarC = TimeMs([&] { TableScalar(csrc.data(), cdst.data(), chromaBytes, g_chroma); }, reps);
		std::printf("%-9s scalar table: luma %.3f ms, chroma %.3f ms\n", s.name, scalarL, scalarC);
#if defined(__aarch64__)
		const double tblL = TimeMs([&] { TableTbl(src.data(), dst.data(), s.luma, g_luma); }, reps);
		const double tblC = TimeMs([&] { TableTbl(csrc.data(), cdst.data(), chromaBytes, g_chroma); }, reps);
		std::printf("%-9s tbl:          luma %.3f ms, chroma %.3f ms\n", s.name, tblL, tblC);
		const double arL = TimeMs([&] { Arith<24673, 85, 146, 16>(src.data(), dst.data(), s.luma); }, reps);
		const double arC = TimeMs([&] { Arith<8224, 8191, 448, 0>(csrc.data(), cdst.data(), chromaBytes); }, reps);
		std::printf("%-9s arithmetic:   luma %.3f ms, chroma %.3f ms\n", s.name, arL, arC);
#endif
	}
	return 0;
}
