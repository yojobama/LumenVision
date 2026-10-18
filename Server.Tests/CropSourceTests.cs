using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using Server;
using Server.Web;
using Xunit;

namespace Server.Tests;

[Collection("ServerSingletons")]
public class CropSourceTests
{
    // a valid 1x1 PNG, enough for an image source to exist
    private const string TinyPng = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";

    private static string WriteImage()
    {
        string path = Path.Combine(Path.GetTempPath(), $"lumen-crop-test-{Guid.NewGuid():N}.png");
        File.WriteAllBytes(path, Convert.FromBase64String(TinyPng));
        return path;
    }

    [Theory]
    [InlineData("left", 1280, 480, 0, 0, 640, 480)]
    [InlineData("right", 1280, 480, 640, 0, 640, 480)]
    [InlineData("top", 640, 480, 0, 0, 640, 240)]
    [InlineData("bottom", 640, 480, 0, 240, 640, 240)]
    [InlineData("left", 1281, 480, 0, 0, 640, 480)]   // an odd width gives the extra column to the right eye, so the two still tile the frame
    [InlineData("right", 1281, 480, 640, 0, 641, 480)]
    public void AStandardSplitCutsTheMatchingHalfOfTheFrame(string layout, int width, int height, int x, int y, int w, int h)
    {
        Assert.Equal(new CropRegionDto(x, y, w, h), SourceManager.RegionFor(layout, width, height));
    }

    [Fact]
    public void ABadSplitNameIs400()
    {
        Assert.Equal(400, Assert.Throws<ApiException>(() => SourceManager.RegionFor("diagonal", 640, 480)).StatusCode);
    }

    [Fact]
    public void ACropRecordKeepsWhatItIsCutFromAndWhereAndOldRecordsStillLoad()
    {
        var crop = new Source(9, "cam left", SourceType.Crop) { CropParentId = 5, CropRegion = new CropRegionDto(0, 0, 640, 480), CropLayout = "left" };

        var restored = JsonSerializer.Deserialize<Source>(JsonSerializer.Serialize(crop))!;

        Assert.Equal(SourceType.Crop, restored.Type);
        Assert.Equal(5, restored.CropParentId);
        Assert.Equal(new CropRegionDto(0, 0, 640, 480), restored.CropRegion);
        Assert.Equal("left", restored.CropLayout);

        var camera = JsonSerializer.Deserialize<Source>("""{"Type":0,"Id":5,"Name":"cam","Profiles":[],"ActiveProfileIndex":-1}""")!;
        Assert.Null(camera.CropParentId);
        Assert.Null(camera.CropRegion);
    }

    [Fact]
    public void ACropOfAnUnknownSourceIs404AndABadRegionIs400()
    {
        Assert.Equal(404, Assert.Throws<ApiException>(() => SourceManager.Instance.InitializeCropSource(987654, "crop", region: new CropRegionDto(0, 0, 10, 10))).StatusCode);

        string path = WriteImage();
        int parent = SourceManager.Instance.initializeImageFileSource(path, "still");
        try
        {
            Assert.Equal(400, Assert.Throws<ApiException>(() => SourceManager.Instance.InitializeCropSource(parent, "crop", region: new CropRegionDto(0, 0, 0, 10))).StatusCode);
            Assert.Equal(400, Assert.Throws<ApiException>(() => SourceManager.Instance.InitializeCropSource(parent, "crop", region: new CropRegionDto(-1, 0, 10, 10))).StatusCode);
            Assert.Equal(400, Assert.Throws<ApiException>(() => SourceManager.Instance.InitializeCropSource(parent, "crop")).StatusCode); // neither a layout nor a region
            // only a camera can be cut in halves: a file's size is not known until it plays
            Assert.Equal(400, Assert.Throws<ApiException>(() => SourceManager.Instance.SplitSource(parent)).StatusCode);
            Assert.DoesNotContain(SourceManager.Instance.GetAllSourceIds(), id => SourceManager.Instance.GetSourceById(id)?.Type == SourceType.Crop);
        }
        finally
        {
            SourceManager.Instance.DeleteSource(parent);
            File.Delete(path);
        }
    }

    [Fact]
    public void ACropIsASourceASinkCanBindToAndItGoesWithItsParent()
    {
        string path = WriteImage();
        int parent = SourceManager.Instance.initializeImageFileSource(path, "still");
        int sink = SinkManager.Instance.AddApriltagSinkWithBackend("tags", 0.1651, ApriltagBackendKind.APRILTAG_BACKEND_CPU, 0, 0);
        try
        {
            int crop = SourceManager.Instance.InitializeCropSource(parent, "still region", region: new CropRegionDto(0, 0, 1, 1));
            Source record = SourceManager.Instance.GetSourceById(crop)!;
            Assert.Equal(SourceType.Crop, record.Type);
            Assert.Equal(parent, record.CropParentId);

            // this threw "no source (or dual-role sink) with id" while crops were unregistered native nodes
            SinkManager.Instance.BindSourceToSink(sink, crop);
            Assert.Equal(crop, SinkManager.Instance.GetSinkById(sink)!.Source!.Id);
            Assert.True(SourceManager.Instance.IsSourceActive(crop)); // binding starts it (and the camera it is cut from)

            SourceManager.Instance.DeleteSource(parent);
            Assert.Null(SourceManager.Instance.GetSourceById(crop));
            Assert.Null(SinkManager.Instance.GetSinkById(sink)!.Source);
        }
        finally
        {
            SinkManager.Instance.DeleteSink(sink);
            if (SourceManager.Instance.GetSourceById(parent) != null) SourceManager.Instance.DeleteSource(parent);
            File.Delete(path);
        }
    }
}
