#include "SystemMonitor.h"

#include <algorithm>
#include <thread>
#include <chrono>

SystemMonitor::SystemMonitor(int timeout)
{
	m_TimeoutMilliseconds = timeout;
}

SystemMonitor::~SystemMonitor()
{
}

void SystemMonitor::StartMonitoring()
{
    m_ThreadWantedAlive = true;
    m_MonitorThread = std::jthread([this] { m_MonitorThreadLoop(); });
}

void SystemMonitor::StopMonitoring()
{
    m_ThreadWantedAlive = false;
    if (m_MonitorThread.joinable()) {
        m_MonitorThread.join();
	}
}

void SystemMonitor::m_MonitorThreadLoop()
{
    while (m_ThreadWantedAlive)
    {
        {
            std::lock_guard<std::mutex> guard(m_CPUUsageMutex);
            m_cpuUsage = m_GetCPUUsage(m_ReadCPUData(), m_ReadCPUData());
        }
        {
            std::lock_guard<std::mutex> guard(m_CPUTemperatureMutex);
            m_cpu_Temperature = ReadCpuTemperatureMilliC("/sys/class/thermal");
        }
        {
            std::lock_guard<std::mutex> guard(m_DiskMutex);
            m_diskUsage = m_GetDiskUsage("/");
        }
        {
            std::lock_guard<std::mutex> guard(m_RAMUsageMutex);
            m_ramUsage = m_ReadMemoryData().get_memory_usage();
        }
		std::this_thread::sleep_for(std::chrono::milliseconds(m_TimeoutMilliseconds));
    }
}

#ifdef __linux__
CPU_STATS SystemMonitor::m_ReadCPUData()
{
    CPU_STATS result{};
    std::ifstream proc_stat("/proc/stat");

    if (proc_stat.good())
    {
        std::string line;
        getline(proc_stat, line);

        unsigned int* stats_p = (unsigned int*)&result;
        std::stringstream iss(line);
        std::string cpu;
        iss >> cpu;
        while (iss >> *stats_p)
        {
            stats_p++;
        };
    }

    proc_stat.close();

    return result;
}
#else
// LUMEN_TODO(windows-system-monitor): /proc/stat has no Windows equivalent; a real implementation belongs on
// GetSystemTimes(). Stubbed at zero so the REST endpoints that read it still return something on every platform.
CPU_STATS SystemMonitor::m_ReadCPUData()
{
    return CPU_STATS{};
}
#endif

int SystemMonitor::m_GetVal(const std::string& target, const std::string& content)
{
    int result = -1;

	std::size_t start = content.find(target);
    if (start != std::string::npos) {
		int begine = start + target.length();
		std::size_t end = content.find("kB", begine);
		std::string substr = content.substr(begine, end - begine);
		result = std::stoi(substr);
    }

    return result;
}

float SystemMonitor::m_GetCPUUsage(const CPU_STATS& first, const CPU_STATS& second)
{
    const int active_diff = second.get_total_active() - first.get_total_active();
    const int idle_diff = second.get_total_idle() - first.get_total_idle();

    const float active_time = static_cast<float>(std::max(active_diff, 0));
    const float idle_time = static_cast<float>(std::max(idle_diff, 0));
    const float total_time = active_time + idle_time;

    if (total_time == 0.0f) {
        return 0.0f;
    }

    return active_time / total_time;
}

#ifdef __linux__
float SystemMonitor::m_GetDiskUsage(const std::string& disk)
{
    struct statvfs diskData;

    statvfs(disk.c_str(), &diskData);

    auto total = diskData.f_blocks;
    auto free = diskData.f_bfree;
    auto diff = total - free;

    float result = static_cast<float>(diff) / total;

    return result;
}
#else
// LUMEN_TODO(windows-system-monitor): statvfs has no Windows equivalent; a real implementation belongs on
// GetDiskFreeSpaceExW(). Stubbed like m_ReadCPUData.
float SystemMonitor::m_GetDiskUsage(const std::string&)
{
    return 0.0f;
}
#endif

static bool ReadFirstLine(const std::string& path, std::string& line)
{
    std::ifstream file(path);
    return file.good() && static_cast<bool>(std::getline(file, line));
}

int ReadCpuTemperatureMilliC(const std::string& thermalRoot)
{
    static const char* const preferred[] = { "x86_pkg_temp", "soc-thermal", "cpu-thermal" };
    int preferredRank = -1;
    int preferredValue = -1;
    int hottest = -1;

    // scan at most 20 thermal zones
    for (int i = 0; i < 20; ++i) {
        const std::string zone = thermalRoot + "/thermal_zone" + std::to_string(i);
        std::string type, temp;
        if (!ReadFirstLine(zone + "/temp", temp)) continue;
        int value = -1;
        try { value = std::stoi(temp); } catch (...) { continue; }
        if (value <= 0) continue;
        hottest = std::max(hottest, value);

        if (!ReadFirstLine(zone + "/type", type)) continue;
        for (int rank = 0; rank < 3; ++rank) {
            if (type == preferred[rank] && (preferredRank < 0 || rank < preferredRank)) {
                preferredRank = rank;
                preferredValue = value;
            }
        }
    }
    return preferredRank >= 0 ? preferredValue : hottest;
}

#ifdef __linux__
MEMORY_STATS SystemMonitor::m_ReadMemoryData()
{
    MEMORY_STATS result{};
    std::ifstream proc_meminfo("/proc/meminfo");

    if (proc_meminfo.good())
    {
        std::string content((std::istreambuf_iterator<char>(proc_meminfo)),
            std::istreambuf_iterator<char>());

        result.total_memory = m_GetVal("MemTotal:", content);
        result.total_swap = m_GetVal("SwapTotal:", content);
        result.free_swap = m_GetVal("SwapFree:", content);
        result.available_memory = m_GetVal("MemAvailable:", content);

    }

    proc_meminfo.close();

    return result;
}
#else
// LUMEN_TODO(windows-system-monitor): /proc/meminfo has no Windows equivalent; a real implementation belongs on
// GlobalMemoryStatusEx(). total_memory=1 avoids a divide-by-zero in get_memory_usage() while reporting 0% used.
MEMORY_STATS SystemMonitor::m_ReadMemoryData()
{
    MEMORY_STATS result{};
    result.total_memory = 1;
    result.available_memory = 1;
    result.total_swap = 1;
    result.free_swap = 1;
    return result;
}
#endif

// get ram usage in megabytes
int SystemMonitor::GetRAMUsage()
{
    std::lock_guard<std::mutex> guard(m_RAMUsageMutex);
	return static_cast<int>(m_ramUsage * 1000);
}

int SystemMonitor::GetCPUTemperature()
{
    std::lock_guard<std::mutex> guard(m_CPUTemperatureMutex);
    return m_cpu_Temperature / 1000; // Convert from millidegrees to degrees
}

float SystemMonitor::GetCPUUsage()
{
    std::lock_guard<std::mutex> guard(m_CPUUsageMutex);
    return m_cpuUsage * 100; // Convert to percentage
}

float SystemMonitor::GetDiskUsage()
{
    std::lock_guard<std::mutex> guard(m_DiskMutex);
    return m_diskUsage * 100; // Convert to percentage
}