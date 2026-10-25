// std::hypotf (what vkapriltag's QuadDecode line fit calls per candidate window) against sqrtf(x*x + y*y): cost per call on this core, and how far apart the results are
// for the magnitudes the fit sees (moments differences, up to a few 1e9).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

template <class F> static double NsPerCall(F f, const std::vector<float>& a, const std::vector<float>& b)
{
	float sink = 0;
	const int reps = 40;
	const auto t0 = std::chrono::steady_clock::now();
	for (int r = 0; r < reps; r++)
		for (size_t i = 0; i < a.size(); i++) sink += f(a[i], b[i]);
	const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / (static_cast<double>(reps) * a.size());
	asm volatile("" : : "r"(sink));
	return ns;
}

int main()
{
	std::mt19937 rng(1);
	std::uniform_real_distribution<float> d(-3e9f, 3e9f);
	std::vector<float> a(1 << 16), b(1 << 16);
	for (size_t i = 0; i < a.size(); i++) { a[i] = d(rng); b[i] = d(rng); }
	const double h = NsPerCall([](float x, float y) { return std::hypotf(x, y); }, a, b);
	const double s = NsPerCall([](float x, float y) { return sqrtf(x * x + y * y); }, a, b);
	const double sd = NsPerCall([](float x, float y) { return static_cast<float>(std::sqrt(static_cast<double>(x) * x + static_cast<double>(y) * y)); }, a, b);
	double worstRel = 0, worstRelD = 0;
	for (size_t i = 0; i < a.size(); i++) {
		const double ref = std::hypot(static_cast<double>(a[i]), static_cast<double>(b[i]));
		worstRel = std::max(worstRel, std::fabs(sqrtf(a[i] * a[i] + b[i] * b[i]) - ref) / ref);
		worstRelD = std::max(worstRelD, std::fabs(static_cast<float>(std::sqrt(static_cast<double>(a[i]) * a[i] + static_cast<double>(b[i]) * b[i])) - ref) / ref);
	}
	std::printf("hypotf %.2f ns/call, sqrtf(x*x+y*y) %.2f ns/call, double sqrt then round %.2f ns/call\n", h, s, sd);
	std::printf("worst relative error against double hypot: float form %.2e, double-then-round form %.2e\n", worstRel, worstRelD);
	return 0;
}
