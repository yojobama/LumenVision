using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using Server;
using Server.Web;
using Xunit;

namespace Server.Tests;

// ObjectDetectionSink tracking settings: persisted on the sink, restored with it, validated, and carried over when a node is copied.
[Collection("ServerSingletons")]
public class ObjectTrackingTests
{
    private static string RepoFile(params string[] parts)
    {
        for (DirectoryInfo? dir = new(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
        {
            string candidate = Path.Combine(new[] { dir.FullName }.Concat(parts).ToArray());
            if (File.Exists(candidate)) return candidate;
        }
        throw new FileNotFoundException(string.Join("/", parts));
    }

    // a detector on the repository's small YOLOv8 test model, and everything it made removed again afterwards
    private sealed class Detector : IDisposable
    {
        private readonly HashSet<int> sinksBefore = new(SinkManager.Instance.getAllSinkIds());
        public Model Model { get; }
        public int Id { get; }

        public Detector()
        {
            using FileStream onnx = File.OpenRead(RepoFile("tests", "data", "yolov8n.onnx"));
            Model = ModelManager.Instance.AddModel("tracking-test-yolo", "tracking-test-yolov8n.onnx", onnx, null, null, YoloVariant.YOLOv8, 640, 0.25f, 0.45f);
            Id = SinkManager.Instance.AddObjectDetectionSink("yolo", Model.Id);
        }

        public void Dispose()
        {
            foreach (int id in SinkManager.Instance.getAllSinkIds().Where(i => !sinksBefore.Contains(i)).ToList())
                try { SinkManager.Instance.DeleteSink(id); } catch (Exception) { }
            ModelManager.Instance.DeleteModel(Model.Id);
        }
    }

    [Fact]
    public void TrackingIsOffWithTheDefaultSettingsUntilSet()
    {
        using var detector = new Detector();
        ObjectTrackingDto tracking = SinkManager.Instance.GetObjectDetectionTracking(detector.Id);
        Assert.False(tracking.Enabled);
        Assert.Equal(0.1, tracking.MinIoU, 6);
        Assert.Equal(3, tracking.MinHits);
        Assert.Equal(500, tracking.MaxAgeMs);
        Assert.True(tracking.ClassAware);
    }

    [Fact]
    public void SettingTrackingReachesTheDetectorAndIsRememberedOnTheSink()
    {
        using var detector = new Detector();
        var wanted = new ObjectTrackingDto(true, 0.25, 5, 800, false);
        SinkManager.Instance.SetObjectDetectionTracking(detector.Id, wanted);

        Assert.Equal(wanted, SinkManager.Instance.GetObjectDetectionTracking(detector.Id));
        // the saved record carries it, and a record from before tracking existed loads with none
        Sink sink = SinkManager.Instance.GetAllSinks().First(s => s.Id == detector.Id);
        Assert.Equal(wanted, sink.ObjectTracking);
        var restored = JsonSerializer.Deserialize<Sink>(JsonSerializer.Serialize(sink))!;
        Assert.Equal(wanted, restored.ObjectTracking);
        var old = JsonSerializer.Deserialize<Sink>("""{"Type":1,"Id":4,"Name":"old","ObjectDetectionModelId":2}""")!;
        Assert.Null(old.ObjectTracking);
    }

    [Theory]
    [InlineData(0.0, 3, 500)]
    [InlineData(1.0, 3, 500)]
    [InlineData(double.NaN, 3, 500)]
    [InlineData(0.1, 0, 500)]
    [InlineData(0.1, 31, 500)]
    [InlineData(0.1, 3, 10)]
    [InlineData(0.1, 3, 60000)]
    public void ValuesOutOfRangeAreRejectedWithA400AndChangeNothing(double minIoU, int minHits, int maxAgeMs)
    {
        using var detector = new Detector();
        var before = SinkManager.Instance.GetObjectDetectionTracking(detector.Id);
        var error = Assert.Throws<ApiException>(() => SinkManager.Instance.SetObjectDetectionTracking(detector.Id, new ObjectTrackingDto(true, minIoU, minHits, maxAgeMs, true)));
        Assert.Equal(400, error.StatusCode);
        Assert.Equal(before, SinkManager.Instance.GetObjectDetectionTracking(detector.Id));
    }

    [Fact]
    public void ASinkThatIsNotAnObjectDetectorIsRejected()
    {
        int id = SinkManager.Instance.AddMjpegSink("preview");
        try
        {
            Assert.Equal(400, Assert.Throws<ApiException>(() => SinkManager.Instance.SetObjectDetectionTracking(id, new ObjectTrackingDto(true, 0.1, 3, 500, true))).StatusCode);
            Assert.Equal(400, Assert.Throws<ApiException>(() => SinkManager.Instance.GetObjectDetectionTracking(id)).StatusCode);
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }

    [Fact]
    public void ACopiedNodeKeepsItsTracking()
    {
        using var detector = new Detector();
        var wanted = new ObjectTrackingDto(true, 0.3, 2, 1200, true);
        SinkManager.Instance.SetObjectDetectionTracking(detector.Id, wanted);

        int copy = SinkManager.Instance.DuplicateSink(detector.Id, false);

        Assert.NotEqual(detector.Id, copy);
        Assert.Equal(wanted, SinkManager.Instance.GetObjectDetectionTracking(copy));
    }
}
