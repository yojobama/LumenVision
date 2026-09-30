using System.Collections.Generic;
using Server;
using Xunit;

namespace Server.Tests;

public class CameraAliasTests
{
    private static Source Camera(int id, string name, int? activeDetector = null) =>
        new(id, name, SourceType.Camera) { ActiveDetectionSinkId = activeDetector };

    [Fact]
    public void ProfileDetectorIsNamedAfterItsCamera()
    {
        var camera = Camera(1, "front", activeDetector: 10);
        var detector = new Sink(10, "front - default", SinkType.ApriltagSink);

        var aliases = NetworkTablesControlService.ComputeCameraAliases(new[] { camera }, new[] { detector });

        Assert.Equal("front", aliases[10]);
    }

    [Fact]
    public void StandaloneDetectorBoundToACameraIsNamedAfterIt()
    {
        var camera = Camera(1, "rear");
        var detector = new Sink(20, "tags", SinkType.ApriltagSink) { Source = camera };

        var aliases = NetworkTablesControlService.ComputeCameraAliases(new[] { camera }, new[] { detector });

        Assert.Equal("rear", aliases[20]);
    }

    [Fact]
    public void OnlyOneDetectorPerCameraGetsTheName()
    {
        var camera = Camera(1, "front", activeDetector: 10);
        var profileDetector = new Sink(10, "front - default", SinkType.ApriltagSink) { Source = camera };
        var standalone = new Sink(11, "extra", SinkType.ApriltagSink) { Source = camera };

        var aliases = NetworkTablesControlService.ComputeCameraAliases(new[] { camera }, new[] { standalone, profileDetector });

        Assert.Equal("front", aliases[10]);
        Assert.False(aliases.ContainsKey(11));
    }

    [Fact]
    public void NonDetectorSinksAndNonCameraSourcesGetNoAlias()
    {
        var file = new Source(2, "clip", SourceType.VideoFile);
        var fileDetector = new Sink(30, "on clip", SinkType.ApriltagSink) { Source = file };
        var preview = new Sink(31, "preview", SinkType.MjpegSink) { Source = Camera(1, "front") };

        var aliases = NetworkTablesControlService.ComputeCameraAliases(new[] { file }, new Sink[] { fileDetector, preview });

        Assert.Empty(aliases);
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
    public void StartsOffAndDefaultModeIsOff()
    {
        var output = new FakeLedOutput();
        using var led = new LedController(output);

        Assert.Equal(LedMode.Default, led.Mode);
        Assert.Equal(new[] { false }, output.Writes);

        led.SetMode(LedMode.On);
        led.SetMode(LedMode.Default);
        Assert.Equal(new[] { false, true, false }, output.Writes);
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
    public void InputSnapshotWritesAFreshFrame()
    {
        int id = SourceManager.Instance.initializeImageFileSource(WriteTinyPng(), "snapshot-test");
        try
        {
            SourceManager.Instance.EnableSourceById(id);

            string? saved = SnapshotService.Instance.Save(id, SnapshotKind.Input);

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
