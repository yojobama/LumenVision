using System.IO;
using System.Text;
using System.Text.Json;
using Server;
using Server.Web;
using Xunit;

namespace Server.Tests;

[Collection("ServerSingletons")]
public class ModelUpdateTests
{
    private static Model AddThrowawayModel()
    {
        using var data = new MemoryStream(Encoding.UTF8.GetBytes("not a real model"));
        return ModelManager.Instance.AddModel("throwaway", "throwaway.onnx", data, null, null, YoloVariant.YOLOv8, 640, 0.25f, 0.45f);
    }

    [Fact]
    public void UpdateChangesOnlyWhatIsGiven()
    {
        Model model = AddThrowawayModel();
        try
        {
            ModelManager.Instance.UpdateModel(model.Id, "renamed", 0.6f, null);

            Model stored = ModelManager.Instance.GetModel(model.Id)!;
            Assert.Equal("renamed", stored.Name);
            Assert.Equal(0.6f, stored.ConfThreshold);
            Assert.Equal(0.45f, stored.NmsThreshold);
            Assert.Equal(640, stored.InputSize);
        }
        finally
        {
            ModelManager.Instance.DeleteModel(model.Id);
        }
    }

    [Theory]
    [InlineData(0.0f, null, null)]
    [InlineData(1.5f, null, null)]
    [InlineData(null, 0.0f, null)]
    [InlineData(null, null, "   ")]
    public void InvalidUpdatesAreRejectedAndChangeNothing(float? conf, float? nms, string? name)
    {
        Model model = AddThrowawayModel();
        try
        {
            Assert.Equal(400, Assert.Throws<ApiException>(() => ModelManager.Instance.UpdateModel(model.Id, name, conf, nms)).StatusCode);
            Assert.Equal("throwaway", ModelManager.Instance.GetModel(model.Id)!.Name);
            Assert.Equal(0.25f, ModelManager.Instance.GetModel(model.Id)!.ConfThreshold);
        }
        finally
        {
            ModelManager.Instance.DeleteModel(model.Id);
        }
    }

    [Fact]
    public void UpdatingAnUnknownModelIs404()
    {
        Assert.Equal(404, Assert.Throws<ApiException>(() => ModelManager.Instance.UpdateModel(987654, "x", null, null)).StatusCode);
    }

    [Fact]
    public void SinkAndProfileRecordsKeepTheirCutoffsAndOldOnesLoadWithNone()
    {
        var sink = new Sink(3, "yolo", SinkType.ObjectDetectionSink) { ObjectDetectionModelId = 1, ObjectDetectionConfThreshold = 0.7f, ObjectDetectionNmsThreshold = 0.3f };
        var restored = JsonSerializer.Deserialize<Sink>(JsonSerializer.Serialize(sink))!;
        Assert.Equal(0.7f, restored.ObjectDetectionConfThreshold);
        Assert.Equal(0.3f, restored.ObjectDetectionNmsThreshold);

        var profile = JsonSerializer.Deserialize<PipelineProfile>("""{"Index":0,"Name":"objects","Kind":1,"ModelId":2}""")!;
        Assert.Null(profile.ConfThreshold);
        Assert.Null(profile.NmsThreshold);
    }

    [Fact]
    public void RetuningASinkThatIsNotAnObjectDetectorIsRejected()
    {
        int id = SinkManager.Instance.AddMjpegSink("preview");
        try
        {
            Assert.Equal(400, Assert.Throws<ApiException>(() => SinkManager.Instance.SetObjectDetectionThresholds(id, 0.5f, 0.5f)).StatusCode);
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }
}
