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
    const std::string thermalRoot = "/sys/class/thermal";
    // CPU usage is the busy share between two /proc/stat samples one interval apart
    CPU_STATS previousCpu = m_ReadCPUData();
    while (m_ThreadWantedAlive)
    {
		std::this_thread::sleep_for(std::chrono::milliseconds(m_TimeoutMilliseconds));

        SystemSnapshot snapshot;
        const CPU_STATS currentCpu = m_ReadCPUData();
        snapshot.cpuUsagePercent = m_GetCPUUsage(previousCpu, currentCpu) * 100.0f;
        previousCpu = currentCpu;

        const int cpuMilliC = ReadCpuTemperatureMilliC(thermalRoot);
        snapshot.cpuTemperatureC = cpuMilliC < 0 ? -1.0 : cpuMilliC / 1000.0;
        for (const auto& [type, milliC] : ReadThermalZonesMilliC(thermalRoot)) snapshot.temperaturesC.emplace_back(type, milliC / 1000.0);
        snapshot.throttled = ReadClockThrottled(thermalRoot);

        const MEMORY_STATS memory = m_ReadMemoryData();
        snapshot.ramTotalMB = memory.total_memory / 1024.0;
        snapshot.ramUsedMB = (memory.total_memory - memory.available_memory) / 1024.0;
        m_ReadDisk("/", snapshot);
        snapshot.uptimeSeconds = m_ReadUptimeSeconds();

        std::lock_guard<std::mutex> guard(m_SnapshotMutex);
        m_Snapshot = std::move(snapshot);
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
void SystemMonitor::m_ReadDisk(const std::string& mountPoint, SystemSnapshot& snapshot)
{
    struct statvfs diskData;
    if (statvfs(mountPoint.c_str(), &diskData) != 0 || diskData.f_blocks == 0) return;

    const double total = static_cast<double>(diskData.f_blocks);
    snapshot.diskUsedPercent = (total - static_cast<double>(diskData.f_bfree)) / total * 100.0;
    snapshot.diskFreeMB = static_cast<double>(diskData.f_bavail) * diskData.f_frsize / (1024.0 * 1024.0);
}

double SystemMonitor::m_ReadUptimeSeconds()
{
    std::ifstream uptime("/proc/uptime");
    double seconds = 0.0;
    uptime >> seconds;
    return uptime ? seconds : 0.0;
}
#else
// LUMEN_TODO(windows-system-monitor): statvfs has no Windows equivalent; a real implementation belongs on
// GetDiskFreeSpaceExW(). Stubbed like m_ReadCPUData.
void SystemMonitor::m_ReadDisk(const std::string&, SystemSnapshot&)
{
}

// LUMEN_TODO(windows-system-monitor): GetTickCount64() is the Windows equivalent of /proc/uptime.
double SystemMonitor::m_ReadUptimeSeconds()
{
    return 0.0;
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

std::vector<std::pair<std::string, int>> ReadThermalZonesMilliC(const std::string& thermalRoot)
{
    std::vector<std::pair<std::string, int>> zones;
    // the same 20-zone scan as ReadCpuTemperatureMilliC
    for (int i = 0; i < 20; ++i) {
        const std::string zone = thermalRoot + "/thermal_zone" + std::to_string(i);
        std::string type, temp;
        if (!ReadFirstLine(zone + "/temp", temp) || !ReadFirstLine(zone + "/type", type)) continue;
        int value = 0;
        try { value = std::stoi(temp); } catch (...) { continue; }
        if (value <= 0) continue;
        zones.emplace_back(type, value);
    }
    return zones;
}

bool ReadClockThrottled(const std::string& thermalRoot)
{
    for (int i = 0; i < 32; ++i) {
        const std::string device = thermalRoot + "/cooling_device" + std::to_string(i);
        std::string type, state;
        if (!ReadFirstLine(device + "/type", type) || !ReadFirstLine(device + "/cur_state", state)) continue;
        if (type.rfind("cpufreq", 0) != 0 && type.rfind("devfreq", 0) != 0) continue;
        try {
            if (std::stoi(state) > 0) return true;
        } catch (...) {
        }
    }
    return false;
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

SystemSnapshot SystemMonitor::GetSnapshot()
{
    std::lock_guard<std::mutex> guard(m_SnapshotMutex);
    return m_Snapshot;
}

int SystemMonitor::GetRAMUsage()
{
    std::lock_guard<std::mutex> guard(m_SnapshotMutex);
	return static_cast<int>(m_Snapshot.ramUsedMB);
}

int SystemMonitor::GetCPUTemperature()
{
    std::lock_guard<std::mutex> guard(m_SnapshotMutex);
    return m_Snapshot.cpuTemperatureC < 0.0 ? 0 : static_cast<int>(m_Snapshot.cpuTemperatureC);
}

float SystemMonitor::GetCPUUsage()
{
    std::lock_guard<std::mutex> guard(m_SnapshotMutex);
    return m_Snapshot.cpuUsagePercent;
}

float SystemMonitor::GetDiskUsage()
{
    std::lock_guard<std::mutex> guard(m_SnapshotMutex);
    return static_cast<float>(m_Snapshot.diskUsedPercent);
}