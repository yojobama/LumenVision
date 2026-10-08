#pragma once
#include <string>

#include <iostream>
#include <fstream>
#include <sstream>
#ifdef __linux__
#include <sys/statvfs.h>
#endif
#include <mutex>
#include <thread>

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

class SystemMonitor
{
public:
	SystemMonitor(int timeout);
	~SystemMonitor();

	void StartMonitoring();
	void StopMonitoring();

	float GetCPUUsage();
	float GetDiskUsage();
	int GetRAMUsage();
	int GetCPUTemperature();
private:
    int m_TimeoutMilliseconds;

	bool m_ThreadWantedAlive;
    std::jthread m_MonitorThread;
	void m_MonitorThreadLoop();

    CPU_STATS m_ReadCPUData();
	int m_GetVal(const std::string& target, const std::string& content);
	float m_GetCPUUsage(const CPU_STATS& first, const CPU_STATS& second);
	float m_GetDiskUsage(const std::string& disk);
	MEMORY_STATS m_ReadMemoryData();
    
    float m_cpuUsage;
    float m_diskUsage;
	int m_ramUsage;
    int m_cpu_Temperature;

	std::mutex m_CPUTemperatureMutex;
	std::mutex m_CPUUsageMutex;
	std::mutex m_DiskMutex;
	std::mutex m_RAMUsageMutex;
};

