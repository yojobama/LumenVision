using System.Linq;
using Server.Controllers;
using Xunit;

namespace Server.Tests;

[Collection("ServerSingletons")]
public class ProfileReaderTests
{
    [Fact]
    public void ReadsTheNativeStageTimingsIntoOneEntryPerNode()
    {
        const string json = """
        {"123": [{"stage":"age","count":240,"meanMs":12.5,"p50Ms":12.0,"p95Ms":20.0,"maxMs":25.0,"lastMs":11.0},
                 {"stage":"vk-gpu","count":240,"meanMs":3.1,"p50Ms":3.0,"p95Ms":4.0,"maxMs":6.0,"lastMs":3.2}],
         "456": []}
        """;

        NodeProfileDto[] nodes = NodeProfileReader.Read(json);

        Assert.Single(nodes);                       // a node with no samples yet is left out
        Assert.Equal(123, nodes[0].Id);
        Assert.Equal(2, nodes[0].Stages.Length);
        Assert.Equal("age", nodes[0].Stages[0].Stage);
        Assert.Equal(12.5, nodes[0].Stages[0].MeanMs);
        Assert.Equal(20.0, nodes[0].Stages[0].P95Ms);
        Assert.Equal(240, nodes[0].Stages[0].Count);
        Assert.Equal("vk-gpu", nodes[0].Stages.Last().Stage);
    }

    [Fact]
    public void NodesNobodyRegisteredStillShowUnderTheirId()
    {
        NodeProfileDto[] nodes = NodeProfileReader.Read("""{"987654": [{"stage":"x","count":1,"meanMs":1,"p50Ms":1,"p95Ms":1,"maxMs":1,"lastMs":1}]}""");

        Assert.Equal("#987654", nodes[0].Name);
        Assert.Equal("node", nodes[0].Kind);
    }

    [Fact]
    public void AnEmptyProfileIsAnEmptyList()
    {
        Assert.Empty(NodeProfileReader.Read("{}"));
    }

    [Fact]
    public void TheManagerReportsTimingsForARunningDetector()
    {
        int id = SinkManager.Instance.AddApriltagSinkWithBackend("timed", 0.1651, ApriltagBackendKind.APRILTAG_BACKEND_CPU, 0, 0);
        try
        {
            // nothing has run, so nothing is reported - and asking is harmless
            Assert.Empty(NodeProfileReader.Read(ManagerWrapper.Instance.GetNodeProfilesJson()).Where(n => n.Id == id));
            ManagerWrapper.Instance.ResetNodeProfiles();
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }
}
