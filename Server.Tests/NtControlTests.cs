using System.Collections.Generic;
using Server;
using Xunit;

namespace Server.Tests;

public class NodeTableTests
{
    private static Source Camera(int id, string name) => new(id, name, SourceType.Camera);

    [Fact]
    public void CamerasAndDetectorsAreNamedAfterThemselves()
    {
        var camera = Camera(1, "front");
        var tags = new Sink(10, "tags", SinkType.ApriltagSink) { Source = camera };

        var tables = NetworkTablesControlService.ComputeNodeTables(new[] { camera }, new[] { tags });

        Assert.Equal("front", tables[1]);
        Assert.Equal("tags", tables[10]);
    }

    [Fact]
    public void NamesAreSanitisedAndMadeUniqueWithTheId()
    {
        var camera = Camera(1, "front cam");
        var first = new Sink(10, "front cam", SinkType.ApriltagSink) { Source = camera };
        var second = new Sink(11, "front cam", SinkType.ObjectDetectionSink) { Source = camera };

        var tables = NetworkTablesControlService.ComputeNodeTables(new[] { camera }, new[] { second, first });

        Assert.Equal("front_cam", tables[1]);
        Assert.Equal("front_cam-10", tables[10]);
        Assert.Equal("front_cam-11", tables[11]);
    }

    [Fact]
    public void ANodeWithoutANameGetsItsId()
    {
        var tables = NetworkTablesControlService.ComputeNodeTables(new[] { Camera(7, "") }, new Sink[] { });

        Assert.Equal("7", tables[7]);
    }

    [Fact]
    public void OtherSinksHaveNoTable()
    {
        var preview = new Sink(31, "preview", SinkType.MjpegSink) { Source = Camera(1, "front") };

        var tables = NetworkTablesControlService.ComputeNodeTables(new[] { Camera(1, "front") }, new[] { preview });

        Assert.False(tables.ContainsKey(31));
    }
}

public class FakeLedOutput : ILedOutput
{
    public List<bool> Writes { get; } = new();
    public bool Disposed { get; private set; }
    public void Set(bool on) { lock (Writes) Writes.Add(on); }
    public void Dispose() => Disposed = true;
}

public class LedControllerTests
{
    [Fact]
    public void StartsOffAndAutoIsOffWithoutADetector()
    {
        var output = new FakeLedOutput();
        using var led = new LedController(output);

        Assert.Equal(LedMode.Auto, led.Mode);
        Assert.Equal(new[] { false }, output.Writes);

        led.SetMode(LedMode.On);
        led.SetMode(LedMode.Auto);
        Assert.Equal(new[] { false, true, false }, output.Writes);
    }

    [Fact]
    public void AutoFollowsWhetherAnyDetectorIsRunning()
    {
        var output = new FakeLedOutput();
        using var led = new LedController(output);

        led.SetAutoActive(true);
        Assert.True(output.Writes[^1]);

        led.SetMode(LedMode.Off);
        led.SetAutoActive(false);
        led.SetAutoActive(true);
        Assert.False(output.Writes[^1]); // a fixed mode ignores the detectors

        led.SetMode(LedMode.Auto);
        Assert.True(output.Writes[^1]);
        led.SetAutoActive(false);
        Assert.False(output.Writes[^1]);
    }

    [Fact]
    public void OnAndOffDriveTheOutput()
    {
        var output = new FakeLedOutput();
        using var led = new LedController(output);

        led.SetMode(LedMode.On);
        Assert.Equal(LedMode.On, led.Mode);
        Assert.True(output.Writes[^1]);

        led.SetMode(LedMode.Off);
        Assert.False(output.Writes[^1]);
    }

    [Fact]
    public void BlinkToggles()
    {
        var output = new FakeLedOutput();
        using var led = new LedController(output);

        led.SetMode(LedMode.Blink);
        Assert.True(output.Writes[^1]);
        System.Threading.Thread.Sleep(1300);
        led.SetMode(LedMode.Off);

        lock (output.Writes)
        {
            // initial off, blink-on, then at least two toggles in 1.3 s, then the final off
            Assert.True(output.Writes.Count >= 5);
            Assert.Contains(false, output.Writes.GetRange(2, output.Writes.Count - 3));
        }
    }

    [Fact]
    public void WithoutAnOutputItStillTracksTheMode()
    {
        using var led = new LedController(null);

        Assert.False(led.Available);
        led.SetMode(LedMode.Blink);
        Assert.Equal(LedMode.Blink, led.Mode);
    }
}

public class SnapshotNamingTests
{
    [Fact]
    public void RelativePathGroupsByCameraAndKind()
    {
        string path = SnapshotService.RelativePath("Front Cam #1", SnapshotKind.Output, new System.DateTime(2026, 9, 30, 14, 5, 9, 42, System.DateTimeKind.Utc));

        Assert.Equal("Front_Cam__1/20260930-140509-042-output.jpg", path);
    }

    [Theory]
    [InlineData("", "camera")]
    [InlineData("..", "camera")]
    [InlineData("../../etc", "_.._etc")]
    [InlineData("ok-name_1.2", "ok-name_1.2")]
    public void CameraNamesCannotEscapeTheSnapshotFolder(string name, string expected)
    {
        string sanitised = SnapshotService.SanitiseName(name);

        Assert.Equal(expected, sanitised);
        Assert.DoesNotContain('/', sanitised);
        Assert.DoesNotContain('\\', sanitised);
    }
}

[Collection("ServerSingletons")]
public class SourceFpsLimitAndSnapshotTests
{
    // a valid 1x1 PNG, so an ImageFileSource can load and publish it
    private static string WriteTinyPng()
    {
        string path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), $"lumen-test-{System.Guid.NewGuid():N}.png");
        System.IO.File.WriteAllBytes(path, System.Convert.FromBase64String(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="));
        return path;
    }

    [Fact]
    public void FpsLimitIsAppliedAndRemembered()
    {
        int id = SourceManager.Instance.initializeImageFileSource(WriteTinyPng(), "fps-test");
        try
        {
            SourceManager.Instance.SetFpsLimit(id, 15);
            Assert.Equal(15, SourceManager.Instance.GetSourceById(id).FpsLimit);
            Assert.Equal(15, ManagerWrapper.Instance.GetSourceFpsLimit(id));

            SourceManager.Instance.SetFpsLimit(id, 0);
            Assert.Null(SourceManager.Instance.GetSourceById(id).FpsLimit);
            Assert.Equal(-1, ManagerWrapper.Instance.GetSourceFpsLimit(id));
        }
        finally
        {
            SourceManager.Instance.DeleteSource(id);
        }
    }

    [Fact]
    public void ACameraSnapshotWritesAFreshFrame()
    {
        int id = SourceManager.Instance.initializeImageFileSource(WriteTinyPng(), "snapshot-test");
        try
        {
            SourceManager.Instance.EnableSourceById(id);

            string? saved = SnapshotService.Instance.SaveNode(id);

            Assert.NotNull(saved);
            Assert.StartsWith("snapshot-test/", saved);
            Assert.True(System.IO.File.Exists(System.IO.Path.Combine(SnapshotService.SnapshotRoot, saved!)));
        }
        finally
        {
            SourceManager.Instance.DeleteSource(id);
            string folder = System.IO.Path.Combine(SnapshotService.SnapshotRoot, "snapshot-test");
            if (System.IO.Directory.Exists(folder)) System.IO.Directory.Delete(folder, true);
        }
    }
}
