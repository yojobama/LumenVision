using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Server;
using Server.Web;
using Xunit;

namespace Server.Tests;

public class PowerAndDataTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), $"lumen-data-{Guid.NewGuid():N}");
    private readonly FakeRunner runner = new();
    private int suspended;

    public PowerAndDataTests() => Directory.CreateDirectory(root);

    public void Dispose() => Directory.Delete(root, true);

    private void Write(string relative, string text = "x")
    {
        string path = Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, text);
    }

    private bool Exists(string relative) => File.Exists(Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar)));

    private PowerService Power() => new(() => runner, new DeviceData(root), TimeSpan.FromMilliseconds(10), () => suspended++);

    private void PopulateEverything()
    {
        Write("data.json");
        Write("settings.json");
        Write("graph-profiles/match.json");
        Write("fieldLayouts/sink-4.json");
        Write("fieldLayouts/bundled/2026-rebuilt-welded.json");
        Write("calibrations.json");
        Write("stereoCalibrations.json");
        Write("models/1_yolo.onnx");
        Write("models/models.json");
        Write("recordings/front/seg0.mp4");
        Write("snapshots/front/a.jpg");
        Write("videos/clip.mp4");
        Write("images/pic.png");
    }

    [Fact]
    public void EachGroupListsOnlyItsOwnFilesAndNeverTheBundledLayouts()
    {
        PopulateEverything();
        var data = new DeviceData(root);

        Assert.Equal(new[] { "data.json", "fieldLayouts/sink-4.json", "graph-profiles/match.json", "settings.json" },
            data.Files(DataGroup.Configuration).OrderBy(f => f, StringComparer.Ordinal).ToArray());
        Assert.Equal(new[] { "calibrations.json", "stereoCalibrations.json" }, data.Files(DataGroup.Calibrations).ToArray());
        Assert.Equal(2, data.Files(DataGroup.Models).Count());
        Assert.Equal(4, data.Files(DataGroup.Media).Count());
    }

    [Theory]
    [InlineData("../outside.txt")]
    [InlineData("a/../../outside.txt")]
    [InlineData("/etc/passwd")]
    [InlineData("C:/Windows/x")]
    [InlineData("")]
    public void ArchivePathsCannotLeaveTheRoot(string relative)
    {
        Assert.Null(new DeviceData(root).SafePath(relative));
    }

    [Fact]
    public void APathInsideTheRootResolves()
    {
        Assert.NotNull(new DeviceData(root).SafePath("models/1_yolo.onnx"));
    }

    [Fact]
    public async Task AFactoryResetKeepsWhatWasAskedForAndRestartsTheServer()
    {
        PopulateEverything();

        int deleted = Power().FactoryReset("Factory Reset", keepCalibrations: true, keepModels: true, keepMedia: false);

        Assert.Equal(8, deleted); // four configuration files and four media files
        Assert.False(Exists("data.json"));
        Assert.False(Exists("settings.json"));
        Assert.False(Exists("graph-profiles/match.json"));
        Assert.False(Exists("fieldLayouts/sink-4.json"));
        Assert.True(Exists("fieldLayouts/bundled/2026-rebuilt-welded.json"));
        Assert.True(Exists("calibrations.json"));
        Assert.True(Exists("models/1_yolo.onnx"));
        Assert.False(Exists("recordings/front/seg0.mp4"));
        Assert.False(Exists("snapshots/front/a.jpg"));
        Assert.Equal(1, suspended);

        await Task.Delay(300);
        Assert.Contains(runner.Calls, c => c.Program == "systemctl" && c.Arguments.SequenceEqual(new[] { "restart", PowerService.ServiceName }));
    }

    [Fact]
    public async Task ResettingEverythingAlsoRemovesCalibrationsAndModels()
    {
        PopulateEverything();

        Power().FactoryReset("factory reset", false, false, false);

        Assert.False(Exists("calibrations.json"));
        Assert.False(Exists("models/models.json"));
        Assert.False(Exists("videos/clip.mp4"));
        Assert.True(Exists("fieldLayouts/bundled/2026-rebuilt-welded.json"));
        await Task.CompletedTask;
    }

    [Theory]
    [InlineData("")]
    [InlineData("reset")]
    [InlineData("yes")]
    public void WithoutTheTypedPhraseNothingIsDeleted(string phrase)
    {
        PopulateEverything();

        var ex = Assert.Throws<ApiException>(() => Power().FactoryReset(phrase, false, false, false));

        Assert.Equal(400, ex.StatusCode);
        Assert.True(Exists("data.json"));
        Assert.Equal(0, suspended);
        Assert.Empty(runner.Calls);
    }

    [Fact]
    public async Task RebootAndShutdownRunTheirCommandsAfterAShortDelay()
    {
        Power().Reboot();
        Power().PowerOff();
        Power().RestartServer();
        await Task.Delay(300);

        Assert.Contains(runner.Calls, c => c.Program == "systemctl" && c.Arguments.SequenceEqual(new[] { "reboot" }));
        Assert.Contains(runner.Calls, c => c.Program == "systemctl" && c.Arguments.SequenceEqual(new[] { "poweroff" }));
        Assert.Contains(runner.Calls, c => c.Program == "systemctl" && c.Arguments.SequenceEqual(new[] { "restart", PowerService.ServiceName }));
    }
}
