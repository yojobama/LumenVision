using System.Text.Json;
using Server;
using Xunit;

namespace Server.Tests;

[Collection("ServerSingletons")]
public class AdvancedTuningTests
{
    [Fact]
    public void NothingGivenMeansNoAdvancedTuning()
    {
        Assert.Null(ApriltagAdvancedTuning.FromQuery(null, null, null, null, null, null, null));
        Assert.NotNull(ApriltagAdvancedTuning.FromQuery(ApriltagFamilyKind.APRILTAG_FAMILY_16H5, null, null, null, null, null, null));
    }

    [Fact]
    public void MergingKeepsTheOldValuesForMembersNotGiven()
    {
        var saved = new ApriltagAdvancedTuning { Family = ApriltagFamilyKind.APRILTAG_FAMILY_25H9, DecisionMargin = 20f };

        var merged = new ApriltagAdvancedTuning { DecisionMargin = 35f, MultiTag = false }.MergedOver(saved);

        Assert.Equal(ApriltagFamilyKind.APRILTAG_FAMILY_25H9, merged.Family);
        Assert.Equal(35f, merged.DecisionMargin);
        Assert.False(merged.MultiTag);
        Assert.Null(merged.QuadSigma);
    }

    [Fact]
    public void RecordsSavedBeforeTheSettingsExistedStillLoad()
    {
        var sink = JsonSerializer.Deserialize<Sink>("""{"Type":0,"Id":7,"Name":"tags","ApriltagTagSize":0.1651}""");
        var profile = JsonSerializer.Deserialize<PipelineProfile>("""{"Index":0,"Name":"default","Kind":0}""");

        Assert.Null(sink!.ApriltagAdvanced);
        Assert.Null(profile!.Advanced);

        var withSettings = JsonSerializer.Deserialize<Sink>(JsonSerializer.Serialize(
            new Sink(8, "x", SinkType.ApriltagSink) { ApriltagAdvanced = new ApriltagAdvancedTuning { Family = ApriltagFamilyKind.APRILTAG_FAMILY_STANDARD41H12 } }));
        Assert.Equal(ApriltagFamilyKind.APRILTAG_FAMILY_STANDARD41H12, withSettings!.ApriltagAdvanced!.Family);
    }

    [Fact]
    public void ASinkKeepsItsAdvancedTuningAcrossABackendSwitchAndAnUpdateOverridesOnlyWhatItGives()
    {
        int id = SinkManager.Instance.AddApriltagSinkWithBackend("tags", 0.1651, ApriltagBackendKind.APRILTAG_BACKEND_CPU, 0, 0,
            advanced: new ApriltagAdvancedTuning { Family = ApriltagFamilyKind.APRILTAG_FAMILY_16H5, QuadSigma = 0.6f, DecisionMargin = 20f });
        try
        {
            ApriltagTuning first = ManagerWrapper.Instance.GetApriltagDetectorEffectiveTuning(id);
            Assert.Equal(ApriltagFamilyKind.APRILTAG_FAMILY_16H5, first.family);
            Assert.Equal(0.6f, first.quadSigma);
            Assert.Equal(20f, first.decisionMargin);

            SinkManager.Instance.SetApriltagBackend(id, ApriltagBackendKind.APRILTAG_BACKEND_CPU,
                advanced: new ApriltagAdvancedTuning { DecisionMargin = 45f, MaxHamming = 1 });

            ApriltagTuning second = ManagerWrapper.Instance.GetApriltagDetectorEffectiveTuning(id);
            Assert.Equal(ApriltagFamilyKind.APRILTAG_FAMILY_16H5, second.family);
            Assert.Equal(0.6f, second.quadSigma);
            Assert.Equal(45f, second.decisionMargin);
            Assert.Equal(1, second.maxHamming);
            Assert.True(ManagerWrapper.Instance.GetApriltagDetectorQuadSigmaSupported(id));
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }
}
