using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using Server;
using Server.Web;
using Xunit;

namespace Server.Tests.Edge;

// F3 and D7 together: a StereoDepthSink saved with its configuration comes back after a restart, with both of its cameras, and its two sources
// can be unbound one at a time. And the board checks of the calibration sessions (C4), which need no camera.
[Collection("ServerSingletons")]
public class StereoDepthRestoreEdgeTests
{
    private const string TinyPng = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";

    private static string WriteImage()
    {
        string path = Path.Combine(Path.GetTempPath(), $"lumen-edge-f-{Guid.NewGuid():N}.png");
        File.WriteAllBytes(path, Convert.FromBase64String(TinyPng));
        return path;
    }

    // identity rectification of a 640x480 pair 10 cm apart, the same numbers LumenCore's own stereo tests use
    private static StereoCalibrationResultDto IdentityCalibration()
    {
        const double f = 600, cx = 320, cy = 240, baseline = 0.1;
        var eye = new CameraCalibrationResultDto(f, f, cx, cy, 0.1, new double[5], 640, 480);
        double[] identity = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        return new StereoCalibrationResultDto(eye, eye, identity, new[] { -baseline, 0, 0 }, new double[9], new double[9],
            identity, identity,
            new[] { f, 0, cx, 0, 0, f, cy, 0, 0, 0, 1, 0 }, new[] { f, 0, cx, -f * baseline, 0, f, cy, 0, 0, 0, 1, 0 },
            new[] { 1, 0, 0, -cx, 0, 1, 0, -cy, 0, 0, 0, f, 0, 0, 1 / baseline, 0 },
            0.1, 0.1, baseline, f, cx, cy, 640, 480, 0, 0, 0, 0, 0, 0, 0, 0);
    }

    [Fact]
    [Trait("Category", "Edge")]
    public void F_AStereoDepthNodeSavedWithItsConfigurationComesBackWithBothCameras()
    {
        var sinks = new HashSet<int>(SinkManager.Instance.getAllSinkIds());
        var sources = new HashSet<int>(SourceManager.Instance.GetAllSourceIds());
        string image = WriteImage();
        try
        {
            var left = new Source(7961, "left", SourceType.ImageFile, image);
            var right = new Source(7962, "right", SourceType.ImageFile, image);
            var depth = new Sink(7951, "depth", SinkType.StereoDepthSink)
            {
                Source = left,
                Source2 = right,
                StereoDepthBackend = StereoDepthBackendKind.STEREO_BACKEND_SGBM,
                StereoDepthCalibration = IdentityCalibration(),
                StereoDepthMinMeters = 0.5,
                StereoDepthMaxMeters = 10,
                StereoDepthMaxSkewUs = 0,
                StereoDepthFrameOutput = default,
            };
            // the record goes through the same JSON as data.json
            File.WriteAllText("data.json", JsonSerializer.Serialize(new { Sinks = new[] { depth }, Sources = new[] { left, right } }));

            DB.Instance.Load();

            Sink? restored = SinkManager.Instance.GetSinkById(7951);
            Assert.NotNull(restored);
            Assert.Equal(7961, restored!.Source?.Id);
            Assert.Equal(7962, restored.Source2?.Id);
            Assert.Equal("sgbm", ManagerWrapper.Instance.GetStereoDepthBackendName(7951).ToLowerInvariant().Split(' ')[0].Trim('(', ')'));

            // D7: one named camera, then the rest
            SinkManager.Instance.UnbindSourceFromSink(7951, 7962);
            Assert.Equal(7961, SinkManager.Instance.GetSinkById(7951)!.Source?.Id);
            Assert.Null(SinkManager.Instance.GetSinkById(7951)!.Source2);
            SinkManager.Instance.UnbindSourceFromSink(7951);
            Assert.Null(SinkManager.Instance.GetSinkById(7951)!.Source);

            // and the pair can be bound again to the same node (the native node was not left holding a camera)
            SinkManager.Instance.BindStereoSourcesToSink(7951, 7961, 7962);
            Assert.Equal(7962, SinkManager.Instance.GetSinkById(7951)!.Source2?.Id);
        }
        finally
        {
            foreach (int id in SinkManager.Instance.getAllSinkIds().Where(i => !sinks.Contains(i)).ToList())
                try { SinkManager.Instance.DeleteSink(id); } catch (Exception) { }
            foreach (int id in SourceManager.Instance.GetAllSourceIds().Where(i => !sources.Contains(i)).ToList())
                try { SourceManager.Instance.DeleteSource(id); } catch (Exception) { }
            File.WriteAllText("data.json", """{"Sinks":[],"Sources":[]}""");
            try { File.Delete(image); } catch (Exception) { }
        }
    }

    [Fact]
    [Trait("Category", "Edge")]
    public void F_ADepthNodeWithShortRectificationArraysIsRefusedWith400()
    {
        StereoCalibrationResultDto calibration = IdentityCalibration() with { R1 = new double[] { 1, 0, 0 } };

        Exception? create = Record.Exception(() => SinkManager.Instance.AddStereoDepthSink("short", StereoDepthBackendKind.STEREO_BACKEND_SGBM,
            calibration.ToNative(), 0.5, 10, 0, default));

        Assert.Equal(400, Assert.IsType<ApiException>(create).StatusCode);
        Assert.DoesNotContain(SinkManager.Instance.GetAllSinks(), s => s.Name == "short");
    }

    [Theory]
    [Trait("Category", "Edge")]
    [InlineData(0, 9, 0.025f)]
    [InlineData(2, 9, 0.025f)]
    [InlineData(6, 0, 0.025f)]
    [InlineData(6, 9, 0.0f)]
    [InlineData(6, 9, float.NaN)]
    public void C_ACalibrationWithAnImpossibleBoardIsRefusedBeforeAnythingIsCreated(int rows, int cols, float squareSizeMeters)
    {
        var nodes = SinkManager.Instance.getAllSinkIds().ToList();

        Exception? camera = Record.Exception(() => CalibrationSessionManager.Instance.StartCamera(1, CalibrationBoardType.BOARD_CHECKERBOARD, rows, cols, squareSizeMeters, 0.018f, 10));
        Exception? stereo = Record.Exception(() => CalibrationSessionManager.Instance.StartStereo(1, 2, CalibrationBoardType.BOARD_CHECKERBOARD, rows, cols, squareSizeMeters));

        Assert.Equal(400, Assert.IsType<ApiException>(camera).StatusCode);
        Assert.Equal(400, Assert.IsType<ApiException>(stereo).StatusCode);
        Assert.Equal(nodes, SinkManager.Instance.getAllSinkIds().ToList());
    }
}
