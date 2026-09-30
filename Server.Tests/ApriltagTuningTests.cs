using System.Text.Json;
using Server;
using Xunit;

namespace Server.Tests;

[Collection("ServerSingletons")]
public class ApriltagRefineModeTests
{
    [Fact]
    public void SinkRecordKeepsTheRefineMode()
    {
        var sink = new Sink(7, "tags", SinkType.ApriltagSink) { ApriltagRefineMode = RefineEdgesMode.REFINE_FAST };

        var restored = JsonSerializer.Deserialize<Sink>(JsonSerializer.Serialize(sink));

        Assert.Equal(RefineEdgesMode.REFINE_FAST, restored!.ApriltagRefineMode);
    }

    [Fact]
    public void RecordsSavedBeforeTheOptionExistedLoadWithNoRefineMode()
    {
        const string oldRecord = """{"Type":0,"Id":7,"Name":"tags","ApriltagTagSize":0.1651,"ApriltagRefineEdges":true}""";

        var restored = JsonSerializer.Deserialize<Sink>(oldRecord);

        Assert.Null(restored!.ApriltagRefineMode);
    }

    [Fact]
    public void ProfilesWithoutARefineModeAndWithTheRetiredCalibratorFieldStillLoad()
    {
        const string oldProfile = """{"Index":0,"Name":"default","Kind":0,"TagSize":0.1651,"CalibratorSinkId":12,"RefineEdges":true}""";

        var profile = JsonSerializer.Deserialize<PipelineProfile>(oldProfile);

        Assert.Null(profile!.RefineMode);
        Assert.Equal(0.1651, profile.TagSize);
    }

    [Fact]
    public void CpuSinkReportsTheRefineModeAsUnsupportedAndUpstream()
    {
        int id = SinkManager.Instance.AddApriltagSinkWithBackend("cpu tags", 0.1651, ApriltagBackendKind.APRILTAG_BACKEND_CPU,
            0, 0, refineMode: RefineEdgesMode.REFINE_ULTRAFAST);
        try
        {
            Assert.False(ManagerWrapper.Instance.GetApriltagDetectorRefineModeSupported(id));
            Assert.Equal(RefineEdgesMode.REFINE_UPSTREAM, ManagerWrapper.Instance.GetApriltagDetectorRefineMode(id));
            Assert.Equal(RefineEdgesMode.REFINE_ULTRAFAST, SinkManager.Instance.GetSinkById(id).ApriltagRefineMode);
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }

    [Fact]
    public void SwitchingBackendCarriesTheRefineModeAndAnExplicitModeOverridesIt()
    {
        int id = SinkManager.Instance.AddApriltagSinkWithBackend("switch tags", 0.1651, ApriltagBackendKind.APRILTAG_BACKEND_CPU,
            0, 0, refineMode: RefineEdgesMode.REFINE_FAST);
        try
        {
            // the Vulkan backend is only built on the first frame, so the requested mode is reported until then
            SinkManager.Instance.SetApriltagBackend(id, ApriltagBackendKind.APRILTAG_BACKEND_VULKAN);
            Assert.Equal(RefineEdgesMode.REFINE_FAST, SinkManager.Instance.GetSinkById(id).ApriltagRefineMode);
            Assert.Equal(RefineEdgesMode.REFINE_FAST, ManagerWrapper.Instance.GetApriltagDetectorRefineMode(id));

            SinkManager.Instance.SetApriltagBackend(id, ApriltagBackendKind.APRILTAG_BACKEND_VULKAN, refineMode: RefineEdgesMode.REFINE_ULTRAFAST);
            Assert.Equal(RefineEdgesMode.REFINE_ULTRAFAST, SinkManager.Instance.GetSinkById(id).ApriltagRefineMode);
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }
}
