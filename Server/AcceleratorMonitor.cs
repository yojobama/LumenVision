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

    // Frequency and load of the board's accelerators. Frequency, governor and (where the driver offers one) load come from devfreq under
    // /sys/class/devfreq; the NPU's load lives in debugfs (/sys/kernel/debug/rknpu/load), which only root can read, so it is read through the
    // privileged runner. Anything a board does not have is left out, so a machine without devfreq simply reports nothing.
    public sealed class AcceleratorMonitor : BackgroundService
    {
        public static AcceleratorMonitor Instance { get; } = new AcceleratorMonitor("/sys/class/devfreq", "/sys/kernel/debug/rknpu/load", () => PrivilegedCommand.Runner);

        private readonly string devfreqRoot;
        private readonly string npuLoadPath;
        private readonly Func<IPrivilegedRunner> runner;
        private readonly TimeSpan interval;
        private volatile AcceleratorInfo[] latest = Array.Empty<AcceleratorInfo>();
        private bool npuLoadUnavailable;

        public AcceleratorMonitor(string devfreqRoot, string npuLoadPath, Func<IPrivilegedRunner> runner, TimeSpan? interval = null)
        {
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

        // One reading of every devfreq device; `npuLoad` replaces the NPU's own load when given.
        public AcceleratorInfo[] Read(double? npuLoad = null)
        {
            if (!Directory.Exists(devfreqRoot)) return Array.Empty<AcceleratorInfo>();
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
            return found.ToArray();
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
