using System.IO;
using System.Linq;
using Server;
using Xunit;

namespace Server.Tests;

[Collection("ServerSingletons")]
public class FieldLayoutTests
{
    [Fact]
    public void TheSeasonsLayoutsAreBundledWithTheirTagCounts()
    {
        var layouts = FieldLayoutCatalog.ListBundled();

        var welded = layouts.Single(l => l.Id == "2026-rebuilt-welded");
        Assert.Equal("2026 REBUILT (welded)", welded.Name);
        Assert.True(welded.TagCount >= 16);
        Assert.Contains(layouts, l => l.Id == "2026-rebuilt-andymark");
        Assert.All(layouts, l => Assert.True(l.TagCount > 0, l.Id));
        // newest season first
        Assert.StartsWith("2026", layouts[0].Id);
    }

    [Theory]
    [InlineData("../Server")]
    [InlineData("..\\Server")]
    [InlineData("a/b")]
    [InlineData("")]
    [InlineData("no-such-layout")]
    public void OnlyRealBundledIdsResolve(string id)
    {
        Assert.Null(FieldLayoutCatalog.ResolveBundled(id));
    }

    [Fact]
    public void ACopiedLayoutLoadsOntoADetector()
    {
        int sinkId = SinkManager.Instance.AddApriltagSinkWithBackend("tags", 0.1651, ApriltagBackendKind.APRILTAG_BACKEND_CPU, 0, 0);
        string destination = Path.Combine(FieldLayoutCatalog.UserDirectory, $"sink-{sinkId}.json");
        try
        {
            int count = FieldLayoutCatalog.CopyBundled("2026-rebuilt-welded", destination);

            Assert.True(ManagerWrapper.Instance.LoadFieldLayout(sinkId, destination));
            Assert.Equal(count, ManagerWrapper.Instance.GetFieldLayoutTagCount(sinkId));
        }
        finally
        {
            SinkManager.Instance.DeleteSink(sinkId);
            if (File.Exists(destination)) File.Delete(destination);
        }
    }

    [Fact]
    public void ANonLayoutFileCountsAsInvalid()
    {
        string path = Path.GetTempFileName();
        try
        {
            File.WriteAllText(path, """{"not":"a layout"}""");
            Assert.Equal(-1, FieldLayoutCatalog.CountTags(path));
            File.WriteAllText(path, "not json at all");
            Assert.Equal(-1, FieldLayoutCatalog.CountTags(path));
            Assert.Equal(-1, FieldLayoutCatalog.CountTags(path + ".missing"));
        }
        finally
        {
            File.Delete(path);
        }
    }
}
