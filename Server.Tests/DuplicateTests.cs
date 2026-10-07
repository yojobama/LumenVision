using System.Linq;
using System.Text.Json;
using Server;
using Server.Web;
using Xunit;

namespace Server.Tests;

[Collection("ServerSingletons")]
public class DuplicateTests
{
    [Fact]
    public void DuplicatingAnApriltagNodeCopiesItsConfigurationUnderANewId()
    {
        int id = SinkManager.Instance.AddApriltagSinkWithBackend("front tags", 0.2, ApriltagBackendKind.APRILTAG_BACKEND_CPU, 0, 0,
            nthreads: 3, quadDecimate: 2.0f, refineEdges: false);
        int copyId = -1;
        try
        {
            ManagerWrapper.Instance.SetDriverMode(id, true);

            copyId = SinkManager.Instance.DuplicateSink(id, copyBindings: false);

            Assert.NotEqual(id, copyId);
            Sink copy = SinkManager.Instance.GetSinkById(copyId);
            Assert.Equal("front tags copy", copy.Name);
            Assert.Equal(0.2, copy.ApriltagTagSize);
            Assert.Equal(3, copy.ApriltagThreads);
            Assert.Equal(2.0f, copy.ApriltagQuadDecimate);
            Assert.False(copy.ApriltagRefineEdges);
            Assert.True(ManagerWrapper.Instance.GetDriverMode(copyId));
            Assert.False(SinkManager.Instance.IsSinkRunning(copyId));
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
            if (copyId >= 0) SinkManager.Instance.DeleteSink(copyId);
        }
    }

    [Fact]
    public void NodesThatAreOutputBadgesCannotBeCopied()
    {
        int id = SinkManager.Instance.AddMjpegSink("preview");
        try
        {
            var ex = Assert.Throws<ApiException>(() => SinkManager.Instance.DuplicateSink(id, false));
            Assert.Equal(400, ex.StatusCode);
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }

    [Fact]
    public void DuplicatingAnUnknownNodeIs404()
    {
        var ex = Assert.Throws<ApiException>(() => SinkManager.Instance.DuplicateSink(987654, false));
        Assert.Equal(404, ex.StatusCode);
    }

    [Fact]
    public void ObjectDetectionRecordsKeepTheirModelId()
    {
        var sink = new Sink(3, "yolo", SinkType.ObjectDetectionSink) { ObjectDetectionModelId = 5 };

        var restored = JsonSerializer.Deserialize<Sink>(JsonSerializer.Serialize(sink));

        Assert.Equal(5, restored!.ObjectDetectionModelId);
    }

    [Fact]
    public void ProfileCopiesAreIndependent()
    {
        var profile = new PipelineProfile { Index = 2, Name = "far", TagSize = 0.1 };

        var copy = profile.Clone();
        copy.Name = "near";

        Assert.Equal("far", profile.Name);
        Assert.Equal(2, copy.Index);
        Assert.Equal(0.1, copy.TagSize);
    }
}
