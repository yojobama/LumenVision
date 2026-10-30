#pragma once
#include <string>

#include <iostream>
#include <fstream>
#include <sstream>
#ifdef __linux__
#include <sys/statvfs.h>
#endif
#include <atomic>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

struct CPU_STATS {
    int user;
    int nice;
    int system;
    int idle;
    int iowait;
    int irq;
    int softirq;
    int steal;
    int guest;
    int guest_nice;

    int get_total_idle()
        const {
        return idle + iowait;
    }

    int get_total_active()
        const {
        return user + nice + system + irq + softirq + steal + guest + guest_nice;
    }
};

struct MEMORY_STATS {
    int total_memory;
    int available_memory;
    int total_swap;
    int free_swap;

    float get_memory_usage() {
        const float result = static_cast<float>(total_memory - available_memory) / total_memory;
        return result;
    }

    float get_swap_usage() {
        const float result = static_cast<float>(total_swap - free_swap) / total_swap;
        return result;
    }
};


// The CPU temperature in millidegrees Celsius from a thermal class directory (/sys/class/thermal), or -1 when none is readable.
// A zone typed x86_pkg_temp (x86) or soc-thermal/cpu-thermal (Rockchip and other ARM boards) is preferred; otherwise the hottest zone is reported.
int ReadCpuTemperatureMilliC(const std::string& thermalRoot);

// Every readable thermal zone under a thermal class directory as (type, millidegrees Celsius), in zone order; on an RK3588 that is the SoC,
// both big-core clusters, the little cores, the centre, the GPU and the NPU.
std::vector<std::pair<std::string, int>> ReadThermalZonesMilliC(const std::string& thermalRoot);

// Whether the kernel's thermal control is holding a clock down: a cooling device that limits a CPU cluster (type cpufreq-*) or a
// GPU/NPU (devfreq-*) is above state 0. Fans are cooling devices too, and are not counted.
bool ReadClockThrottled(const std::string& thermalRoot);

// One reading of the board's state; published to the robot as "<rootTable>/.status" (see NetworkTablesSink).
struct SystemSnapshot {
	float cpuUsagePercent = 0.0f;
	double cpuTemperatureC = -1.0; // the zone ReadCpuTemperatureMilliC picks; -1 when none is readable
	std::vector<std::pair<std::string, double>> temperaturesC; // every zone, see ReadThermalZonesMilliC
	bool throttled = false;        // see ReadClockThrottled
	double ramUsedMB = 0.0;        // MemTotal - MemAvailable
	double ramTotalMB = 0.0;
	double diskUsedPercent = 0.0;  // the root file system, which recordings and snapshots are written to
	double diskFreeMB = 0.0;       // what an unprivileged writer can still use
	double uptimeSeconds = 0.0;    // the board's, from /proc/uptime: a reboot resets it, a service restart does not
};

class SystemMonitor
{
public:
	SystemMonitor(int timeout);
	~SystemMonitor();

	void StartMonitoring();
	void StopMonitoring();

	// the latest reading; all zero (temperature -1) until the monitor thread's first interval has passed
	SystemSnapshot GetSnapshot();

	float GetCPUUsage();
	float GetDiskUsage();
	// RAM in use, in megabytes
	int GetRAMUsage();
	// degrees Celsius; 0 when no thermal zone is readable
	int GetCPUTemperature();
private:
    int m_TimeoutMilliseconds;

	std::atomic<bool> m_ThreadWantedAlive{ false }; // read by the monitor thread, cleared by StopMonitoring()
    std::jthread m_MonitorThread;
	void m_MonitorThreadLoop();

    CPU_STATS m_ReadCPUData();
	int m_GetVal(const std::string& target, const std::string& content);
	float m_GetCPUUsage(const CPU_STATS& first, const CPU_STATS& second);
	// fills the snapshot's root file system fields
	void m_ReadDisk(const std::string& mountPoint, SystemSnapshot& snapshot);
	MEMORY_STATS m_ReadMemoryData();
	double m_ReadUptimeSeconds();

	std::mutex m_SnapshotMutex;
	SystemSnapshot m_Snapshot; // guarded by m_SnapshotMutex
};

