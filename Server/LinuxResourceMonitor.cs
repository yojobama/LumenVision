using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

public class SystemResourceInfo
{
    public double CpuUsagePercent { get; set; }
    public double TotalMemoryMB { get; set; }
    public double UsedMemoryMB { get; set; }
    public double FreeMemoryMB { get; set; }
    public DiskInfo RootDiskUsage { get; set; } = new DiskInfo();
    public DateTime LastUpdateTime { get; set; } = DateTime.MinValue;
}

public class DiskInfo
{
    public string MountPoint { get; set; } = "/";
    public long TotalSizeGB { get; set; }
    public long UsedSpaceGB { get; set; }
    public long AvailableSpaceGB { get; set; }
    public double UsedPercent { get; set; }
}

/// <summary>
/// Singleton class for monitoring key system resources on a Linux machine.
/// </summary>
public class LinuxResourceMonitor
{
    // --- Singleton Setup ---
    private static readonly LinuxResourceMonitor _instance = new LinuxResourceMonitor();
    public static LinuxResourceMonitor Instance => _instance;

    // --- Monitoring & Thread Safety ---
    private readonly object _lock = new object();
    private CancellationTokenSource? _cts;
    private Task? _monitorTask;
    
    // Data storage
    private SystemResourceInfo _latestInfo = new SystemResourceInfo();
    private (long Total, long Idle) _previousCpuTimes = (0, 0);
    
    // Constants
    private const string ProcStatPath = "/proc/stat";
    private const string MemInfoPath = "/proc/meminfo";
    private const int MonitorIntervalMs = 2000; // Update data every 2 seconds

    private LinuxResourceMonitor() { }

    // --- Control Methods ---

    /// <summary>
    /// Starts the internal monitoring thread if it is not already running.
    /// </summary>
    public void StartMonitoring()
    {
        lock (_lock)
        {
            if (_monitorTask == null || _monitorTask.IsCompleted)
            {
                // Reset CPU times on start/restart for accurate calculation
                _previousCpuTimes = (0, 0); 
                _cts = new CancellationTokenSource();
                _monitorTask = Task.Run(() => MonitorLoop(_cts.Token));
                Console.WriteLine("Resource monitor started.");
            }
        }
    }

    /// <summary>
    /// Stops the internal monitoring thread.
    /// </summary>
    public void StopMonitoring()
    {
        lock (_lock)
        {
            if (_cts != null)
            {
                _cts.Cancel();
                _monitorTask?.Wait(MonitorIntervalMs * 2); // Wait for the task to gracefully finish
                _cts.Dispose();
                _cts = null;
                _monitorTask = null;
                Console.WriteLine("Resource monitor stopped.");
            }
        }
    }

    /// <summary>
    /// Stops the monitor and immediately starts it again.
    /// </summary>
    public void RestartMonitoring()
    {
        StopMonitoring();
        StartMonitoring();
    }

    /// <summary>
    /// Retrieves the latest collected system resource information in a thread-safe manner.
    /// </summary>
    public SystemResourceInfo GetLatestResourceInfo()
    {
        lock (_lock)
        {
            // Return a shallow copy of the object to prevent external modification of internal state
            return _latestInfo;
        }
    }

    // --- Internal Monitoring Logic ---

    private async Task MonitorLoop(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                var newInfo = new SystemResourceInfo();
                
                newInfo.CpuUsagePercent = await CalculateCpuUsageAsync();
                
                var (total, used, free) = GetMemoryUtilization();
                newInfo.TotalMemoryMB = total;
                newInfo.UsedMemoryMB = used;
                newInfo.FreeMemoryMB = free;

                newInfo.RootDiskUsage = GetRootStorageUtilization();
                
                newInfo.LastUpdateTime = DateTime.Now;

                lock (_lock)
                {
                    _latestInfo = newInfo;
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error during monitoring: {ex.Message}");
            }

            await Task.Delay(MonitorIntervalMs, cancellationToken);
        }
    }

    // --- Data Collection Implementations ---

    private async Task<double> CalculateCpuUsageAsync()
    {
        // CPU usage requires two readings to calculate the delta over time.
        var currentTimes = ReadCpuTimes();

        if (_previousCpuTimes.Total == 0)
        {
            // First call: just store the baseline reading and return 0
            _previousCpuTimes = currentTimes;
            // Introduce a short delay to ensure there is a measurable difference on the next loop iteration
            await Task.Delay(500); 
            return 0.0; 
        }

        long totalDiff = currentTimes.Total - _previousCpuTimes.Total;
        long idleDiff = currentTimes.Idle - _previousCpuTimes.Idle;
        _previousCpuTimes = currentTimes; // Update baseline

        if (totalDiff <= 0)
        {
            return 0.0;
        }

        // CPU Used % = 100 * (1 - (Idle time / Total time))
        return 100.0 * (1.0 - (double)idleDiff / totalDiff);
    }

    private (long Total, long Idle) ReadCpuTimes()
    {
        try
        {
            string line = File.ReadLines(ProcStatPath).FirstOrDefault();
            if (string.IsNullOrEmpty(line) || !line.StartsWith("cpu "))
            {
                return (0, 0);
            }

            // Skip "cpu" and parse all time components (user, nice, system, idle, iowait, etc.)
            var parts = line.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries)
                            .Skip(1) 
                            .Select(x => long.Parse(x, CultureInfo.InvariantCulture))
                            .ToArray();

            long total = parts.Sum();
            // Idle time is the sum of the 4th (idle) and 5th (iowait) fields in the parts array
            long idle = parts.Length > 4 ? parts[3] + parts[4] : parts[3];

            return (total, idle);
        }
        catch { return (0, 0); }
    }
    
    /// <summary>
    /// Gets the overall system RAM utilization (Total, Used, Available) in Megabytes.
    /// Reads /proc/meminfo.
    /// </summary>
    private (double TotalMB, double UsedMB, double AvailableMB) GetMemoryUtilization()
    {
        long total = 0, free = 0, buffers = 0, cached = 0;

        try
        {
            var lines = File.ReadAllLines(MemInfoPath);
            foreach (var line in lines)
            {
                if (line.StartsWith("MemTotal:"))
                    total = long.Parse(line.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries)[1]);
                else if (line.StartsWith("MemFree:"))
                    free = long.Parse(line.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries)[1]);
                else if (line.StartsWith("Buffers:"))
                    buffers = long.Parse(line.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries)[1]);
                else if (line.StartsWith("Cached:"))
                    cached = long.Parse(line.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries)[1]);
            }

            // Linux calculation for usable memory (Available) = Free + Buffers + Cached
            long available = free + buffers + cached;
            long used = total - available;

            // Convert from Kilobytes to Megabytes
            double totalMB = total / 1024.0;
            double usedMB = used / 1024.0;
            double availableMB = available / 1024.0; 

            return (totalMB, usedMB, availableMB);
        }
        catch { return (0, 0, 0); }
    }

    /// <summary>
    /// Gets the storage utilization only for the root mount point '/'.
    /// </summary>
    private DiskInfo GetRootStorageUtilization()
    {
        try
        {
            // DriveInfo.GetDrives() returns all mounted drives. We look for '/' (root).
            DriveInfo rootDrive = DriveInfo.GetDrives().FirstOrDefault(d => d.Name == "/");

            if (rootDrive != null && rootDrive.IsReady)
            {
                long totalSize = rootDrive.TotalSize;
                long availableFreeSpace = rootDrive.AvailableFreeSpace;
                long usedSpace = totalSize - availableFreeSpace;

                double usedPercent = (double)usedSpace / totalSize * 100.0;

                const long BytesInGB = 1024 * 1024 * 1024;

                return new DiskInfo
                {
                    MountPoint = rootDrive.Name,
                    TotalSizeGB = totalSize / BytesInGB,
                    UsedSpaceGB = usedSpace / BytesInGB,
                    AvailableSpaceGB = availableFreeSpace / BytesInGB,
                    UsedPercent = usedPercent
                };
            }
        }
        catch { /* Return default empty info on failure */ }

        return new DiskInfo { MountPoint = "/" };
    }
}


public class ExampleProgram
{
    public static async Task Main(string[] args)
    {
        var monitor = LinuxResourceMonitor.Instance;

        monitor.StartMonitoring();

        // Give the monitor time to stabilize (especially for CPU reading)
        Console.WriteLine("Giving the monitoring thread 3 seconds to collect initial data...");
        await Task.Delay(3000); 

        Console.WriteLine("\n--- Real-Time Monitoring Loop (Check for 10 seconds) ---");
        for (int i = 0; i < 5; i++)
        {
            var info = monitor.GetLatestResourceInfo();

            Console.WriteLine("--------------------------------------------------");
            Console.WriteLine($"[Sample {i + 1}] Updated: {info.LastUpdateTime:HH:mm:ss}");
            Console.WriteLine($"CPU Usage: {info.CpuUsagePercent:F2}%");
            Console.WriteLine($"RAM Used/Total: {info.UsedMemoryMB:F0} MB / {info.TotalMemoryMB:F0} MB");
            
            var disk = info.RootDiskUsage;
            Console.WriteLine($"Root Disk ({disk.MountPoint}): {disk.UsedPercent:F2}% Used ({disk.UsedSpaceGB:N0} GB / {disk.TotalSizeGB:N0} GB)");

            await Task.Delay(2000);
        }
        Console.WriteLine("--------------------------------------------------\n");

        Console.WriteLine("Restarting monitor...");
        monitor.RestartMonitoring();
        await Task.Delay(3000); 

        var finalInfo = monitor.GetLatestResourceInfo();
        Console.WriteLine($"\n[Restart Check] New Update Time: {finalInfo.LastUpdateTime:HH:mm:ss}");
        Console.WriteLine($"New CPU Reading: {finalInfo.CpuUsagePercent:F2}%");
        
        monitor.StopMonitoring();
    }
}
