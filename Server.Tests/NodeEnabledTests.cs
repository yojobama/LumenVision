using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using Server;
using Xunit;

namespace Server.Tests;

[Collection("ServerSingletons")]
public class NodeEnabledTests
{
    private static int AddDetector() =>
        SinkManager.Instance.AddApriltagSinkWithBackend("tags", 0.1651, ApriltagBackendKind.APRILTAG_BACKEND_CPU, 0, 0);

    [Fact]
    public void ADetectorSwitchedOffStaysListedAsDisabledUntilSwitchedOn()
    {
        int id = AddDetector();
        try
        {
            SinkManager.Instance.SetSinkEnabled(id, true);
            Assert.True(SinkManager.Instance.IsSinkRunning(id));
            Assert.False(SinkManager.Instance.IsNodeDisabled(id));

            SinkManager.Instance.SetSinkEnabled(id, false);
            Assert.False(SinkManager.Instance.IsSinkRunning(id));
            Assert.Contains(id, SinkManager.Instance.GetDisabledNodeIds());
            using (JsonDocument saved = JsonDocument.Parse(DB.Instance.GetJson()))
                Assert.Contains(id, saved.RootElement.GetProperty("DisabledNodes").EnumerateArray().Select(e => e.GetInt32()));

            SinkManager.Instance.SetSinkEnabled(id, true);
            Assert.True(SinkManager.Instance.IsSinkRunning(id));
            Assert.DoesNotContain(id, SinkManager.Instance.GetDisabledNodeIds());
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }

    [Fact]
    public void ADisabledDetectorStaysDisabledWhenItIsRebuilt()
    {
        int id = AddDetector();
        try
        {
            SinkManager.Instance.SetSinkEnabled(id, false);
            SinkManager.Instance.SetApriltagBackend(id, ApriltagBackendKind.APRILTAG_BACKEND_CPU, nthreads: 2);
            Assert.False(SinkManager.Instance.IsSinkRunning(id));
            Assert.True(SinkManager.Instance.IsNodeDisabled(id));
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }

    [Fact]
    public void RestoredDisabledNodesThatNoLongerExistAreDropped()
    {
        SinkManager.Instance.RestoreDisabledNodes(new[] { 987654 });
        Assert.True(SinkManager.Instance.IsNodeDisabled(987654));
        Assert.Empty(SinkManager.Instance.GetDisabledNodeIds());
        SinkManager.Instance.RestoreDisabledNodes(Array.Empty<int>());
    }

    [Fact]
    public void RecordingANodeReusesOneRecordSinkAndStopsOnlyThatNode()
    {
        int a = AddDetector();
        int b = AddDetector();
        try
        {
            SinkManager.Instance.SetNodeRecording(a, true);
            Assert.True(SinkManager.Instance.IsNodeRecording(a));
            Assert.False(SinkManager.Instance.IsNodeRecording(b));
            int recordSinks = SinkManager.Instance.GetAllSinks().Count(s => s.Type == SinkType.RecordSink && s.Source?.Id == a);

            SinkManager.Instance.SetNodeRecording(a, true); // idempotent: no second RecordSink
            Assert.Equal(recordSinks, SinkManager.Instance.GetAllSinks().Count(s => s.Type == SinkType.RecordSink && s.Source?.Id == a));

            SinkManager.Instance.SetNodeRecording(b, true);
            SinkManager.Instance.SetNodeRecording(a, false);
            Assert.False(SinkManager.Instance.IsNodeRecording(a));
            Assert.True(SinkManager.Instance.IsNodeRecording(b));

            // a recording that was stopped stays stopped after a restart: its RecordSink is in the saved disabled list
            int stoppedRecordSink = SinkManager.Instance.GetAllSinks().Single(s => s.Type == SinkType.RecordSink && s.Source?.Id == a).Id;
            Assert.Contains(stoppedRecordSink, SinkManager.Instance.GetDisabledNodeIds());

            SinkManager.Instance.SetAllRecording(false);
            Assert.False(SinkManager.Instance.IsNodeRecording(b));
        }
        finally
        {
            foreach (Sink recordSink in SinkManager.Instance.GetAllSinks().Where(s => s.Type == SinkType.RecordSink && (s.Source?.Id == a || s.Source?.Id == b)).ToList())
                SinkManager.Instance.DeleteSink(recordSink.Id);
            SinkManager.Instance.DeleteSink(a);
            SinkManager.Instance.DeleteSink(b);
        }
    }

    [Fact]
    public void RecordingAnUnknownNodeIsRejected()
    {
        Assert.Throws<Server.Web.ApiException>(() => SinkManager.Instance.SetNodeRecording(987654, true));
    }

    [Fact]
    public void SwitchingAnUnknownSinkIsRejected()
    {
        Assert.ThrowsAny<Exception>(() => SinkManager.Instance.SetSinkEnabled(987654, false));
    }

    [Fact]
    public void ASnapshotOfAnUnknownNodeIsRejected()
    {
        Assert.Throws<ArgumentException>(() => SnapshotService.Instance.SaveNode(987654));
    }

    [Fact]
    public void TheGraphListsEveryNodeWithItsTableNameRunningStateAndInput()
    {
        var camera = new Source(1, "front", SourceType.Camera);
        var crop = new Source(2, "front-left", SourceType.Crop) { CropParentId = 1 };
        var detector = new Sink(10, "front - tags", SinkType.ApriltagSink) { Source = camera };
        var preview = new Sink(11, "preview", SinkType.MjpegSink) { Source = camera };

        string json = NetworkTablesControlService.BuildGraphJson(
            new[] { camera, crop }, new[] { detector, preview }, new Dictionary<int, string> { [10] = "front-tags", [1] = "front" }, id => id == 10, id => id == 1);

        JsonElement nodes = JsonDocument.Parse(json).RootElement.GetProperty("nodes");
        JsonElement Node(int id) => nodes.EnumerateArray().Single(n => n.GetProperty("id").GetInt32() == id);
        Assert.Equal(4, nodes.GetArrayLength());
        Assert.Equal("front - tags", Node(10).GetProperty("name").GetString());
        Assert.Equal("front-tags", Node(10).GetProperty("table").GetString());
        Assert.Equal("11", Node(11).GetProperty("table").GetString());
        Assert.Equal("front", Node(1).GetProperty("table").GetString());
        Assert.True(Node(1).GetProperty("recording").GetBoolean());
        Assert.False(Node(10).GetProperty("recording").GetBoolean());
        Assert.True(Node(10).GetProperty("enabled").GetBoolean());
        Assert.Equal(1, Node(10).GetProperty("input").GetInt32());
        Assert.False(Node(11).GetProperty("enabled").GetBoolean());
        Assert.Equal("preview", Node(11).GetProperty("name").GetString());
        Assert.Equal(1, Node(2).GetProperty("input").GetInt32());
        Assert.Equal(JsonValueKind.Null, Node(1).GetProperty("input").ValueKind);
    }
}
