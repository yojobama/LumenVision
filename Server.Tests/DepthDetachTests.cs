using System;
using Server;
using Xunit;

namespace Server.Tests;

[Collection("ServerSingletons")]
public class DepthDetachTests
{
    [Fact]
    public void DetachingADepthSourceClearsItAndIsRepeatable()
    {
        int id = SinkManager.Instance.AddDepthFusionSink("fusion");
        try
        {
            SinkManager.Instance.GetSinkById(id).DepthSourceId = 42; // as an attach leaves it
            SinkManager.Instance.DetachDepthFusionSource(id);
            Assert.Null(SinkManager.Instance.GetSinkById(id).DepthSourceId);
            SinkManager.Instance.DetachDepthFusionSource(id); // nothing attached any more: still fine
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }

    [Fact]
    public void OnlyADepthFusionSinkCanBeDetached()
    {
        int id = SinkManager.Instance.AddApriltagSinkWithBackend("tags", 0.1651, ApriltagBackendKind.APRILTAG_BACKEND_CPU, 0, 0);
        try
        {
            Assert.Throws<Exception>(() => SinkManager.Instance.DetachDepthFusionSource(id));
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }
}
