using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Server;
using Xunit;

namespace Server.Tests;

public class AcceleratorMonitorTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), $"lumen-devfreq-{Guid.NewGuid():N}");
    private readonly FakeRunner runner = new();

    public void Dispose()
    {
        if (Directory.Exists(root)) Directory.Delete(root, true);
    }

    private void Device(string name, long curHz, long maxHz, string governor, string? load = null)
    {
        string directory = Path.Combine(root, name);
        Directory.CreateDirectory(directory);
        File.WriteAllText(Path.Combine(directory, "cur_freq"), curHz.ToString());
        File.WriteAllText(Path.Combine(directory, "max_freq"), maxHz.ToString());
        File.WriteAllText(Path.Combine(directory, "governor"), governor + "\n");
        if (load != null) File.WriteAllText(Path.Combine(directory, "load"), load);
    }

    private AcceleratorMonitor Monitor() => new(root, "/sys/kernel/debug/rknpu/load", () => runner);

    [Theory]
    [InlineData("fb000000.gpu", "GPU")]
    [InlineData("ff400000.gpu-mali", "GPU")]
    [InlineData("dmc", "Memory")]
    [InlineData("fdab0000.npu", "NPU")]
    [InlineData("something-else", "Other")]
    public void DevicesAreClassifiedByName(string name, string kind)
    {
        Assert.Equal(kind, AcceleratorMonitor.Classify(name));
    }

    [Theory]
    [InlineData("37@800000000Hz", 37.0)]
    [InlineData("0@0Hz", 0.0)]
    [InlineData("12", 12.0)]
    [InlineData("12%", 12.0)]
    [InlineData("12.5@100Hz", 12.5)]
    [InlineData("", null)]
    [InlineData("busy", null)]
    public void DevfreqLoadFilesAreParsed(string text, double? expected)
    {
        Assert.Equal(expected, AcceleratorMonitor.ParseLoad(text));
    }

    [Theory]
    [InlineData("NPU load:  Core0:  12%, Core1:   0%, Core2:  40%,", 40.0)]
    [InlineData("NPU load:  0%", 0.0)]
    [InlineData("no percentages here", null)]
    public void TheNpuLoadIsTheBusiestCore(string text, double? expected)
    {
        Assert.Equal(expected, AcceleratorMonitor.ParseNpuLoad(text));
    }

    [Fact]
    public void EveryDevfreqDeviceIsReadWithFrequencyGovernorAndLoad()
    {
        Device("fb000000.gpu", 702_000_000, 1_000_000_000, "performance", "35@702000000Hz");
        Device("dmc", 2_112_000_000, 2_112_000_000, "performance");
        Directory.CreateDirectory(Path.Combine(root, "broken")); // no cur_freq: skipped

        AcceleratorInfo[] read = Monitor().Read();

        Assert.Equal(2, read.Length);
        var gpu = read.Single(a => a.Kind == "GPU");
        Assert.Equal(702.0, gpu.FreqMhz);
        Assert.Equal(1000.0, gpu.MaxFreqMhz);
        Assert.Equal("performance", gpu.Governor);
        Assert.Equal(35.0, gpu.LoadPercent);
        Assert.Null(read.Single(a => a.Kind == "Memory").LoadPercent);
    }

    [Fact]
    public void AMachineWithoutDevfreqReportsNothing()
    {
        Assert.Empty(new AcceleratorMonitor(Path.Combine(root, "missing"), "x", () => runner).Read());
    }

    [Fact]
    public async Task TheNpuLoadComesFromDebugfsThroughThePrivilegedRunner()
    {
        Device("fdab0000.npu", 1_000_000_000, 1_000_000_000, "performance");
        runner.Respond = (program, args) => program == "cat" && args[0] == "/sys/kernel/debug/rknpu/load"
            ? new CommandResult(0, "NPU load:  Core0:  22%, Core1:   5%, Core2:   0%,\n", "")
            : new CommandResult(1, "", "no");
        AcceleratorMonitor monitor = Monitor();

        await monitor.PollAsync();

        Assert.Equal(22.0, monitor.Latest.Single().LoadPercent);
    }

    [Fact]
    public async Task WhenDebugfsCannotBeReadTheNpuStillShowsItsFrequencyAndStopsAsking()
    {
        Device("fdab0000.npu", 500_000_000, 1_000_000_000, "performance");
        runner.Respond = (_, _) => new CommandResult(1, "", "No such file");
        AcceleratorMonitor monitor = Monitor();

        await monitor.PollAsync();
        await monitor.PollAsync();

        Assert.Null(monitor.Latest.Single().LoadPercent);
        Assert.Equal(500.0, monitor.Latest.Single().FreqMhz);
        Assert.Single(runner.Calls); // asked once, then never again
    }

    [Fact]
    public async Task WithoutAnNpuNothingPrivilegedRuns()
    {
        Device("fb000000.gpu", 700_000_000, 1_000_000_000, "performance");

        await Monitor().PollAsync();

        Assert.Empty(runner.Calls);
    }
}
