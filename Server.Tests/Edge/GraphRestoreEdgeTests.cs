using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using Server;
using Xunit;

namespace Server.Tests.Edge;

// Edge cases of saving and restoring the node graph (data.json -> DB.Load -> DB.Verify, the startup path in Program.Main).
[Collection("ServerSingletons")]
public class GraphRestoreEdgeTests
{
    // a valid 1x1 PNG, enough for an image source to exist
    private const string TinyPng = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";

    private static string WriteImage()
    {
        string path = Path.Combine(Path.GetTempPath(), $"lumen-edge-f-{Guid.NewGuid():N}.png");
        File.WriteAllBytes(path, Convert.FromBase64String(TinyPng));
        return path;
    }

    // Deletes every sink and source a test added (whatever state a Load left them in) and leaves an empty graph on disk.
    private sealed class GraphScope : IDisposable
    {
        private readonly HashSet<int> sinks = new(SinkManager.Instance.getAllSinkIds());
        private readonly HashSet<int> sources = new(SourceManager.Instance.GetAllSourceIds());
        public readonly List<string> TempPaths = new();

        public void Dispose()
        {
            foreach (int id in SinkManager.Instance.getAllSinkIds().Where(i => !sinks.Contains(i)).ToList())
                try { SinkManager.Instance.DeleteSink(id); } catch (Exception) { }
            foreach (int id in SourceManager.Instance.GetAllSourceIds().Where(i => !sources.Contains(i)).ToList())
                try { SourceManager.Instance.DeleteSource(id); } catch (Exception) { }
            File.WriteAllText("data.json", """{"Sinks":[],"Sources":[]}""");
            foreach (string path in TempPaths)
                try { if (Directory.Exists(path)) Directory.Delete(path, true); else File.Delete(path); } catch (Exception) { }
        }
    }

    private static void WriteGraph(IEnumerable<Sink> sinks, IEnumerable<Source> sources) =>
        File.WriteAllText("data.json", JsonSerializer.Serialize(new { Sinks = sinks.ToList(), Sources = sources.ToList() }));

    // F1: the UI's model page says "Detectors already made from it keep running until restarted". After that restart RestoreObjectDetectionSink
    // drops the record (its model is gone), then DB.Verify sees the record missing and re-adds it through AddSink("ObjectDetectionSink"), whose
    // no-model native overload always throws - and Program.Main calls Verify() unguarded, so the server never starts.
    [Fact]
    [Trait("Category", "Edge")]
    public void F_ADetectorWhoseModelWasDeletedDoesNotStopTheServerStarting()
    {
        using var scope = new GraphScope();
        WriteGraph(new[] { new Sink(7701, "yolo", SinkType.ObjectDetectionSink) { ObjectDetectionModelId = 987654 } }, Array.Empty<Source>());

        DB.Instance.Load();
        Exception? startup = Record.Exception(() => DB.Instance.Verify());

        Assert.Null(startup);
    }

    // F1 variant: a detector record from before model tracking (no ObjectDetectionModelId) takes the same path.
    [Fact]
    [Trait("Category", "Edge")]
    public void F_AnOldDetectorRecordWithoutAModelDoesNotStopTheServerStarting()
    {
        using var scope = new GraphScope();
        WriteGraph(new[] { new Sink(7702, "old yolo", SinkType.ObjectDetectionSink) }, Array.Empty<Source>());

        DB.Instance.Load();
        Exception? startup = Record.Exception(() => DB.Instance.Verify());

        Assert.Null(startup);
    }

    // F2a: deleting a detector removes its WebRTC/MJPEG previews but leaves every other node that reads it (its RecordSink, a NetworkTablesSink,
    // a depth fusion node) bound to an id that no longer exists, and that binding is saved to data.json.
    [Fact]
    [Trait("Category", "Edge")]
    public void F_DeletingADetectorUnbindsTheNodesThatReadIt()
    {
        using var scope = new GraphScope();
        int tags = SinkManager.Instance.AddApriltagSinkWithBackend("tags", 0.1651, ApriltagBackendKind.APRILTAG_BACKEND_CPU, 0, 0);
        SinkManager.Instance.SetNodeRecording(tags, true); // the "record this node" button: a RecordSink bound to the detector
        int recorder = SinkManager.Instance.GetAllSinks().Single(s => s.Type == SinkType.RecordSink && s.Source?.Id == tags).Id;
        SinkManager.Instance.SetNodeRecording(tags, false);

        SinkManager.Instance.DeleteSink(tags);

        Assert.Null(SinkManager.Instance.GetSinkById(recorder)?.Source);
        using JsonDocument saved = JsonDocument.Parse(DB.Instance.GetJson());
        Assert.DoesNotContain(saved.RootElement.GetProperty("Sinks").EnumerateArray(),
            s => s.GetProperty("Source").ValueKind == JsonValueKind.Object && s.GetProperty("Source").GetProperty("Id").GetInt32() == tags);
    }

    // F2b: what F2a leaves on disk. On the next start the dangling binding throws inside DB.LoadInternal's single try/catch, which abandons
    // every binding after it and every sink start: a detector listed later comes back unbound and stopped (and the next Save makes it permanent).
    [Fact]
    [Trait("Category", "Edge")]
    public void F_ABindingToADeletedNodeDoesNotStopTheRestOfTheGraphRestoring()
    {
        using var scope = new GraphScope();
        string image = WriteImage();
        string recordings = Path.Combine(Path.GetTempPath(), $"lumen-edge-f-rec-{Guid.NewGuid():N}");
        scope.TempPaths.Add(image);
        scope.TempPaths.Add(recordings);
        var still = new Source(7801, "still", SourceType.ImageFile, image);
        WriteGraph(new[]
        {
            new Sink(7802, "tags-rec", SinkType.RecordSink) { Source = new Source(7899, "deleted tags", SourceType.SinkOutput), RecordDstFolder = recordings },
            new Sink(7803, "tags", SinkType.ApriltagSink) { ApriltagTagSize = 0.1651, Source = still },
        }, new[] { still });

        DB.Instance.Load();

        Assert.Equal(7801, SinkManager.Instance.GetSinkById(7803)?.Source?.Id);
        Assert.True(SinkManager.Instance.IsSinkRunning(7803));
    }

    // F3: a bound StereoDepthSink is never rebuilt by DB.Load (AddSink(id) has no case for it), yet its saved binding is replayed:
    // BindStereoSourcesToSink throws "no sink with id", which abandons the rest of the restore exactly like F2b - on every restart.
    [Fact]
    [Trait("Category", "Edge")]
    public void F_ASavedStereoDepthNodeDoesNotStopTheRestOfTheGraphRestoring()
    {
        using var scope = new GraphScope();
        string image = WriteImage();
        scope.TempPaths.Add(image);
        var left = new Source(7911, "left", SourceType.ImageFile, image);
        var right = new Source(7912, "right", SourceType.ImageFile, image);
        WriteGraph(new[]
        {
            new Sink(7901, "depth", SinkType.StereoDepthSink) { Source = left, Source2 = right },
            new Sink(7902, "tags", SinkType.ApriltagSink) { ApriltagTagSize = 0.1651, Source = left },
        }, new[] { left, right });

        DB.Instance.Load();

        Assert.Equal(7911, SinkManager.Instance.GetSinkById(7902)?.Source?.Id);
        Assert.True(SinkManager.Instance.IsSinkRunning(7902));
    }

    // F4: DisableSinkById means to stop the source once no sink uses it, but counts the sink being disabled as a user, so the source is never
    // stopped: switching off the only detector of a camera (or crop) leaves it capturing and decoding forever. Shown on a crop source, which
    // runs a thread of its own even when cut from a still image (an image source has no thread to observe; no camera is attached here).
    [Fact]
    [Trait("Category", "Edge")]
    public void F_SwitchingOffTheOnlyDetectorOfASourceStopsTheSource()
    {
        using var scope = new GraphScope();
        string image = WriteImage();
        scope.TempPaths.Add(image);
        int still = SourceManager.Instance.initializeImageFileSource(image, "still");
        int crop = SourceManager.Instance.InitializeCropSource(still, "still crop", region: new CropRegionDto(0, 0, 1, 1));
        int tags = SinkManager.Instance.AddApriltagSinkWithBackend("tags", 0.1651, ApriltagBackendKind.APRILTAG_BACKEND_CPU, 0, 0);
        SinkManager.Instance.BindSourceToSink(tags, crop);
        SinkManager.Instance.SetSinkEnabled(tags, true);
        Assert.True(SourceManager.Instance.IsSourceActive(crop));

        SinkManager.Instance.SetSinkEnabled(tags, false);

        Assert.False(SourceManager.Instance.IsSourceActive(crop));
    }
}
