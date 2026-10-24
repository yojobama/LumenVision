#include "CpuAffinity.h"
#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>
#include <algorithm>
#include <string>
#include <thread>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <fstream>
#endif

namespace CpuAffinity
{

#ifdef __linux__
static long ReadSysfsLong(const std::string& path)
{
	std::ifstream file(path);
	long value = 0;
	if (file.is_open()) file >> value;
	return value;
}

static std::vector<int> DetectPerformanceCores()
{
	const unsigned int cpuCount = std::thread::hardware_concurrency();
	// Each core's speed: the kernel's cpu_capacity when every core publishes it (1024 for the fastest core), otherwise the maximum
	// scaling frequency. RK3588 A76 clusters are binned separately, so they can report slightly different frequencies.
	std::vector<long> capacity(cpuCount), maxFreq(cpuCount);
	bool haveCapacity = cpuCount > 0;
	for (unsigned int cpu = 0; cpu < cpuCount; ++cpu) {
		const std::string dir = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
		capacity[cpu] = ReadSysfsLong(dir + "/cpu_capacity");
		maxFreq[cpu] = ReadSysfsLong(dir + "/cpufreq/cpuinfo_max_freq");
		if (capacity[cpu] <= 0) haveCapacity = false;
	}
	const std::vector<long>& speed = haveCapacity ? capacity : maxFreq;

	const long fastest = speed.empty() ? 0 : *std::max_element(speed.begin(), speed.end());
	if (fastest <= 0) return {}; // nothing readable

	// the performance set is every core within 10% of the fastest, so two A76 clusters binned a few percent apart stay together
	std::vector<int> performance;
	for (unsigned int cpu = 0; cpu < cpuCount; ++cpu) {
		if (speed[cpu] * 10 >= fastest * 9) performance.push_back(static_cast<int>(cpu));
	}

	// every core in one tier means uniform (non-big.LITTLE) hardware: there is no performance core set to pin to
	if (performance.size() == cpuCount) return {};
	return performance;
}
#endif

const std::vector<int>& GetPerformanceCoreIds()
{
#ifdef __linux__
	static const std::vector<int> cores = DetectPerformanceCores();
#else
	static const std::vector<int> cores;
#endif
	return cores;
}

void PinCurrentThreadToPerformanceCores()
{
#ifdef __linux__
	const std::vector<int>& cores = GetPerformanceCoreIds();
	if (cores.empty()) return;

	cpu_set_t cpuSet;
	CPU_ZERO(&cpuSet);
	for (int core : cores) CPU_SET(core, &cpuSet);
	// best-effort - a failure here (e.g. cgroup/affinity restrictions in a container) just
	// leaves the thread on the OS scheduler's default placement, not worth failing over.
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuSet);
#endif
}

ScopedPerformanceAffinity::ScopedPerformanceAffinity()
{
#ifdef __linux__
	const std::vector<int>& cores = GetPerformanceCoreIds();
	if (cores.empty()) return;
	static_assert(sizeof(cpu_set_t) <= sizeof(m_Saved), "saved affinity mask too small");
	cpu_set_t* saved = reinterpret_cast<cpu_set_t*>(m_Saved);
	if (pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), saved) != 0) return;
	m_Changed = true;
	PinCurrentThreadToPerformanceCores();
#endif
}

ScopedPerformanceAffinity::~ScopedPerformanceAffinity()
{
#ifdef __linux__
	if (m_Changed) pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), reinterpret_cast<const cpu_set_t*>(m_Saved));
#endif
}

void NameCurrentThread(const std::string& prefix, const std::string& id)
{
#ifdef __linux__
	// the kernel keeps at most 15 characters
	std::string name = prefix + "-" + id;
	if (name.size() > 15) name.resize(15);
	pthread_setname_np(pthread_self(), name.c_str());
#else
	(void)prefix;
	(void)id;
#endif
}

void InitialiseOpenCvThreads()
{
	// OpenCV's worker pool starts on its first parallel call and its threads inherit the affinity of the thread that made that
	// call; a short-lived pinned thread makes that call, so the pool stays on the performance cores instead of following
	// whichever (possibly .NET) thread ran first
	std::thread([] {
		PinCurrentThreadToPerformanceCores();
		const std::vector<int>& cores = GetPerformanceCoreIds();
		cv::setNumThreads(cores.empty() ? 4 : static_cast<int>(cores.size()));
		cv::Mat warm(64, 64, CV_8UC1, cv::Scalar(0));
		cv::parallel_for_(cv::Range(0, 64), [&](const cv::Range& rows) {
			for (int y = rows.start; y < rows.end; ++y) warm.row(y).setTo(cv::Scalar(1));
		});
	}).join();
}

} // namespace CpuAffinity
