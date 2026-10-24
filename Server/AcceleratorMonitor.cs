using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Hosting;

namespace Server
{
    // "GPU", "Memory" (the DRAM controller), "NPU" or "Other"
    public sealed record AcceleratorInfo(string Kind, string Name, double FreqMhz, double MaxFreqMhz, string Governor, double? LoadPercent);

    // Frequency and load of the board's accelerators, and the frequency of each CPU cluster (a cluster running well under its maximum is how
    // thermal throttling shows). Frequency, governor and (where the driver offers one) load come from devfreq under
    // /sys/class/devfreq; the NPU's load lives in debugfs (/sys/kernel/debug/rknpu/load), which only root can read, so it is read through the
    // privileged runner. Anything a board does not have is left out, so a machine without devfreq simply reports nothing.
    public sealed class AcceleratorMonitor : BackgroundService
    {
        public static AcceleratorMonitor Instance { get; } = new AcceleratorMonitor("/sys/class/devfreq", "/sys/kernel/debug/rknpu/load", () => PrivilegedCommand.Runner, cpufreqRoot: "/sys/devices/system/cpu/cpufreq");

        private readonly string devfreqRoot;
        private readonly string npuLoadPath;
        private readonly string? cpufreqRoot;
        private readonly Func<IPrivilegedRunner> runner;
        private readonly TimeSpan interval;
        private volatile AcceleratorInfo[] latest = Array.Empty<AcceleratorInfo>();
        private bool npuLoadUnavailable;

        public AcceleratorMonitor(string devfreqRoot, string npuLoadPath, Func<IPrivilegedRunner> runner, TimeSpan? interval = null, string? cpufreqRoot = null)
        {
            this.cpufreqRoot = cpufreqRoot;
            this.devfreqRoot = devfreqRoot;
            this.npuLoadPath = npuLoadPath;
            this.runner = runner;
            this.interval = interval ?? TimeSpan.FromSeconds(2);
        }

        public AcceleratorInfo[] Latest => latest;

        public static string Classify(string name)
        {
            string lower = name.ToLowerInvariant();
            if (lower.Contains("gpu") || lower.Contains("mali")) return "GPU";
            if (lower.Contains("dmc") || lower.Contains("ddr") || lower.Contains("memory")) return "Memory";
            if (lower.Contains("npu")) return "NPU";
            return "Other";
        }

        // devfreq's "load" file: "37@800000000Hz" on Rockchip's DMC and GPU drivers, or a bare percentage. Null when it says nothing usable.
        public static double? ParseLoad(string text)
        {
            Match match = Regex.Match(text.Trim(), @"^(\d+(?:\.\d+)?)\s*(?:@|%|$)");
            return match.Success ? double.Parse(match.Groups[1].Value, CultureInfo.InvariantCulture) : null;
        }

        // rknpu's debugfs load: "NPU load:  Core0:  12%, Core1:   0%, Core2:   0%," - the busiest core is what limits throughput
        public static double? ParseNpuLoad(string text)
        {
            var loads = Regex.Matches(text, @"(\d+)\s*%").Select(m => double.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture)).ToList();
            return loads.Count == 0 ? null : loads.Max();
        }

        private static double? ReadHertz(string path)
        {
            try
            {
                return File.Exists(path) && double.TryParse(File.ReadAllText(path).Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out double hz) ? hz / 1_000_000.0 : null;
            }
            catch (IOException)
            {
                return null;
            }
        }

        private static string? ReadText(string path)
        {
            try
            {
                return File.Exists(path) ? File.ReadAllText(path).Trim() : null;
            }
            catch (IOException)
            {
                return null;
            }
        }

        // "4 5 6 7" (a policy's related_cpus) as "4-7"; a non-contiguous list stays a list
        public static string DescribeCpus(string related)
        {
            var cpus = related.Split(' ', StringSplitOptions.RemoveEmptyEntries).Select(int.Parse).OrderBy(c => c).ToList();
            if (cpus.Count == 0) return "";
            bool contiguous = cpus.Zip(cpus.Skip(1), (a, b) => b == a + 1).All(x => x);
            return cpus.Count == 1 ? cpus[0].ToString() : contiguous ? $"{cpus[0]}-{cpus[^1]}" : string.Join(",", cpus);
        }

        // One entry per cpufreq policy (one per CPU cluster); frequencies there are in kHz
        private IEnumerable<AcceleratorInfo> ReadCpuClusters()
        {
            if (cpufreqRoot == null || !Directory.Exists(cpufreqRoot)) yield break;
            foreach (string directory in Directory.EnumerateDirectories(cpufreqRoot, "policy*").OrderBy(d => d, StringComparer.Ordinal))
            {
                if (!double.TryParse(ReadText(Path.Combine(directory, "scaling_cur_freq")), NumberStyles.Float, CultureInfo.InvariantCulture, out double currentKhz)) continue;
                double maxKhz = double.TryParse(ReadText(Path.Combine(directory, "cpuinfo_max_freq")), NumberStyles.Float, CultureInfo.InvariantCulture, out double max) ? max : currentKhz;
                string cpus = DescribeCpus(ReadText(Path.Combine(directory, "related_cpus")) ?? "");
                yield return new AcceleratorInfo("CPU", cpus.Length > 0 ? $"CPU cores {cpus}" : Path.GetFileName(directory),
                    Math.Round(currentKhz / 1000.0, 1), Math.Round(maxKhz / 1000.0, 1), ReadText(Path.Combine(directory, "scaling_governor")) ?? "", null);
            }
        }

        // One reading of every devfreq device and CPU cluster; `npuLoad` replaces the NPU's own load when given.
        public AcceleratorInfo[] Read(double? npuLoad = null)
        {
            var found = new List<AcceleratorInfo>();
            if (Directory.Exists(devfreqRoot)) found.AddRange(ReadDevfreq(npuLoad));
            found.AddRange(ReadCpuClusters());
            return found.ToArray();
        }

        private List<AcceleratorInfo> ReadDevfreq(double? npuLoad)
        {
            var found = new List<AcceleratorInfo>();
            foreach (string directory in Directory.EnumerateDirectories(devfreqRoot).OrderBy(d => d, StringComparer.Ordinal))
            {
                string name = Path.GetFileName(directory);
                if (ReadHertz(Path.Combine(directory, "cur_freq")) is not double current) continue;
                string kind = Classify(name);
                string? loadText = ReadText(Path.Combine(directory, "load"));
                double? load = loadText == null ? null : ParseLoad(loadText);
                if (kind == "NPU" && npuLoad.HasValue) load = npuLoad;
                found.Add(new AcceleratorInfo(kind, name, Math.Round(current, 1), Math.Round(ReadHertz(Path.Combine(directory, "max_freq")) ?? current, 1),
                    ReadText(Path.Combine(directory, "governor")) ?? "", load));
            }
            return found;
        }

        private async Task<double?> ReadNpuLoadAsync()
        {
            if (npuLoadUnavailable) return null;
            try
            {
                CommandResult result = await runner().RunAsync("cat", new[] { npuLoadPath }, TimeSpan.FromSeconds(3), quiet: true);
                if (!result.Ok)
                {
                    // no NPU driver, or debugfs is not mounted: stop asking
                    npuLoadUnavailable = true;
                    return null;
                }
                return ParseNpuLoad(result.StdOut);
            }
            catch (Exception ex) when (ex is PlatformNotSupportedException or TimeoutException or System.ComponentModel.Win32Exception)
            {
                npuLoadUnavailable = true;
                return null;
            }
        }

        // one polling step (the loop below calls it; tests call it directly)
        public async Task PollAsync()
        {
            AcceleratorInfo[] first = Read();
            double? npuLoad = first.Any(a => a.Kind == "NPU") ? await ReadNpuLoadAsync() : null;
            latest = npuLoad.HasValue ? Read(npuLoad) : first;
        }

        protected override async Task ExecuteAsync(CancellationToken stoppingToken)
        {
            while (!stoppingToken.IsCancellationRequested)
            {
                try
                {
                    await PollAsync();
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Reading accelerator stats failed: {ex.Message}");
                }
                try
                {
                    await Task.Delay(interval, stoppingToken);
                }
                catch (OperationCanceledException)
                {
                    return;
                }
            }
        }
    }
}
