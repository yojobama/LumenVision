using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Server;
using Server.Controllers;
using Server.Web;
using Xunit;

namespace Server.Tests.Edge;

// Non-finite and out-of-range numbers reaching the server through its REST parameters. ASP.NET binds "NaN", "Infinity" and "1e40" to float/double,
// and System.Text.Json refuses to write a non-finite number, so one such value stored on a sink or model makes every later DB.Save (data.json),
// ModelManager.Save (models.json) and StateChannel tick (it serialises every Sink) throw until the server restarts.
[Collection("ServerSingletons")]
public class NumericInputEdgeTests
{
    private static string RepoFile(params string[] parts)
    {
        for (DirectoryInfo? dir = new(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
        {
            string candidate = Path.Combine(new[] { dir.FullName }.Concat(parts).ToArray());
            if (File.Exists(candidate)) return candidate;
        }
        throw new FileNotFoundException(string.Join("/", parts));
    }

    private static void DeleteSinksAddedSince(HashSet<int> before)
    {
        // non-finite records first, so the Save inside DeleteSink can succeed again
        foreach (int id in SinkManager.Instance.getAllSinkIds().Where(i => !before.Contains(i)).ToList())
            try { SinkManager.Instance.DeleteSink(id); } catch (Exception) { }
    }

    // F5a: no validation at all on the AprilTag tag size (or quadDecimate / quadSigma / decisionMargin) - a NaN is stored on the record.
    [Theory]
    [Trait("Category", "Edge")]
    [InlineData(double.NaN)]
    [InlineData(double.PositiveInfinity)]
    public void F_ANonFiniteTagSizeIsRejectedAndTheGraphCanStillBeSaved(double tagSize)
    {
        var before = new HashSet<int>(SinkManager.Instance.getAllSinkIds());
        try
        {
            Exception? create = Record.Exception(() =>
                SinkManager.Instance.AddApriltagSinkWithBackend("nan tags", tagSize, ApriltagBackendKind.APRILTAG_BACKEND_CPU, 0, 0));

            Assert.Null(Record.Exception(() => DB.Instance.Save()));
            Assert.Null(Record.Exception(() => JsonSerializer.Serialize(SinkManager.Instance.GetAllSinks())));
            Assert.Equal(400, Assert.IsType<ApiException>(create).StatusCode);
        }
        finally
        {
            DeleteSinksAddedSince(before);
        }
    }

    // F5b: UpdateModel's "is < 0.01f or > 1.0f" range check is false for NaN, so NaN passes it and is saved on the model.
    [Fact]
    [Trait("Category", "Edge")]
    public void F_ANaNModelCutoffIsRejectedLikeAnyOtherOutOfRangeValue()
    {
        using var data = new MemoryStream(Encoding.UTF8.GetBytes("not a real model"));
        Model model = ModelManager.Instance.AddModel("edge-f-nan", "edge-f-nan.onnx", data, null, null, YoloVariant.YOLOv8, 640, 0.25f, 0.45f);
        try
        {
            Exception? update = Record.Exception(() => ModelManager.Instance.UpdateModel(model.Id, null, float.NaN, null));

            Assert.Equal(400, Assert.IsType<ApiException>(update).StatusCode);
            Assert.Equal(0.25f, ModelManager.Instance.GetModel(model.Id)!.ConfThreshold);
        }
        finally
        {
            Model? stored = ModelManager.Instance.GetModel(model.Id);
            if (stored != null) { stored.ConfThreshold = 0.25f; ModelManager.Instance.DeleteModel(model.Id); }
        }
    }

    // F5c: SetObjectDetectionThresholds has the same NaN hole as UpdateModel, on a live detector (PATCH objectDetectionSink/thresholds).
    [Fact]
    [Trait("Category", "Edge")]
    public void F_ANaNDetectorCutoffIsRejectedAndTheGraphCanStillBeSaved()
    {
        var before = new HashSet<int>(SinkManager.Instance.getAllSinkIds());
        Model model;
        using (FileStream onnx = File.OpenRead(RepoFile("tests", "data", "yolov8n.onnx")))
            model = ModelManager.Instance.AddModel("edge-f-yolo", "edge-f-yolov8n.onnx", onnx, null, null, YoloVariant.YOLOv8, 640, 0.25f, 0.45f);
        try
        {
            int detector = SinkManager.Instance.AddObjectDetectionSink("yolo", model.Id);

            Exception? retune = Record.Exception(() => SinkManager.Instance.SetObjectDetectionThresholds(detector, float.NaN, 0.45f));

            Assert.Null(Record.Exception(() => DB.Instance.Save()));
            Assert.Equal(400, Assert.IsType<ApiException>(retune).StatusCode);
        }
        finally
        {
            DeleteSinksAddedSince(before);
            ModelManager.Instance.DeleteModel(model.Id);
        }
    }

    // F6: POST model/upload validates nothing it parses: cutoffs outside 0.01-1 (which PATCH model/update rejects), NaN, a zero or negative input
    // size. A NaN is stored in the in-memory list before models.json is written, so the upload 500s, the model stays listed, and every later
    // upload, update and delete of any model 500s too (models.json can no longer be written).
    [Theory]
    [Trait("Category", "Edge")]
    [InlineData("NaN", "640")]
    [InlineData("5", "640")]
    [InlineData("-1", "640")]
    [InlineData("0.25", "0")]
    [InlineData("0.25", "-640")]
    public async Task F_AnUploadWithAnInvalidCutoffOrInputSizeIsRejectedAndNothingIsStored(string confThreshold, string inputSize)
    {
        string name = $"edge-f-upload-{Guid.NewGuid():N}";
        using var form = new MultipartFormDataContent
        {
            { new StringContent(name), "name" },
            { new StringContent("0"), "variant" },
            { new StringContent(inputSize), "inputSize" },
            { new StringContent(confThreshold), "confThreshold" },
            { new StringContent("0.45"), "nmsThreshold" },
            { new ByteArrayContent(Encoding.UTF8.GetBytes("not a real model")), "model", "edge.onnx" },
        };
        var body = new MemoryStream();
        await form.CopyToAsync(body);
        body.Position = 0;
        var context = new DefaultHttpContext();
        context.Request.Method = "POST";
        context.Request.ContentType = form.Headers.ContentType!.ToString();
        context.Request.Body = body;
        var controller = new ModelController { ControllerContext = new ControllerContext { HttpContext = context } };

        try
        {
            Exception? upload = await Record.ExceptionAsync(() => controller.Upload());

            Assert.DoesNotContain(ModelManager.Instance.GetAllModels(), m => m.Name == name);
            Assert.Equal(400, Assert.IsType<ApiException>(upload).StatusCode);
        }
        finally
        {
            foreach (Model stored in ModelManager.Instance.GetAllModels().Where(m => m.Name == name).ToList())
            {
                stored.ConfThreshold = 0.25f; // so the Save inside DeleteModel can write models.json again
                ModelManager.Instance.DeleteModel(stored.Id);
            }
        }
    }
}
