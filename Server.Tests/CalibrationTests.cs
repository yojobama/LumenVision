using System.Runtime.CompilerServices;
using System.Text.Json;
using Server;
using Server.Web;
using Xunit;

namespace Server.Tests;

// The Server singletons read and write their JSON files relative to the working directory, so it is set to a
// scratch directory before any of them is first touched.
internal static class TestEnvironment
{
    [ModuleInitializer]
    internal static void UseScratchDirectory()
    {
        string dir = Path.Combine(Path.GetTempPath(), "lumenvision-server-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        Environment.CurrentDirectory = dir;
    }
}

public class CalibrationResultPersistenceTests
{
    private static CameraCalibrationResult MonoResult(int width, int height, double fx) =>
        new(fx, fx, width / 2.0, height / 2.0, 0.3, new VectorDouble(), width, height);

    [Fact]
    public void CameraResultIsSavedUnderTheCameraPathAndResolution()
    {
        string path = "/dev/video-persist-test";
        CalibrationManager.Instance.SaveResult(path, MonoResult(1280, 720, 900));

        var stored = CalibrationManager.Instance.GetLatest(path, 1280, 720);
        Assert.NotNull(stored);
        Assert.Equal(900, stored!.Result.fx);
        Assert.Null(CalibrationManager.Instance.GetLatest(path, 640, 480));
        Assert.Null(CalibrationManager.Instance.GetLatest("/dev/other-camera", 1280, 720));
    }

    [Fact]
    public void SavingAgainAtTheSameResolutionReplacesTheEarlierResult()
    {
        string path = "/dev/video-replace-test";
        CalibrationManager.Instance.SaveResult(path, MonoResult(640, 480, 500));
        CalibrationManager.Instance.SaveResult(path, MonoResult(640, 480, 520));

        Assert.Single(CalibrationManager.Instance.GetAll(), c => c.CameraPath == path && c.Result.imageWidth == 640);
        Assert.Equal(520, CalibrationManager.Instance.GetLatest(path, 640, 480)!.Result.fx);
    }

    [Fact]
    public void ResultWithoutACameraPathIsNotPersisted()
    {
        int before = CalibrationManager.Instance.GetAll().Count;
        CalibrationManager.Instance.SaveResult(null, MonoResult(320, 240, 300));
        Assert.Equal(before, CalibrationManager.Instance.GetAll().Count);
    }

    [Fact]
    public void StereoResultIsSavedUnderBothCameraPaths()
    {
        var result = new StereoCalibrationResult { imageWidth = 1280, imageHeight = 720, baselineMeters = 0.06 };
        StereoCalibrationManager.Instance.SaveResult("/dev/video-left", "/dev/video-right", result);
        StereoCalibrationManager.Instance.SaveResult("/dev/video-left", null, result);

        var stored = StereoCalibrationManager.Instance.GetLatest("/dev/video-left", "/dev/video-right", 1280, 720);
        Assert.NotNull(stored);
        Assert.Equal(0.06, stored!.Result.baselineMeters);
        Assert.DoesNotContain(StereoCalibrationManager.Instance.GetAll(), c => c.RightCameraPath == "");
    }
}

public class CalibrationSessionTests
{
    [Fact]
    public void StartingOnAnUnknownSourceIsNotFound()
    {
        var ex = Assert.Throws<ApiException>(() =>
            CalibrationSessionManager.Instance.StartCamera(987654, CalibrationBoardType.BOARD_CHECKERBOARD, 6, 9, 0.025f, 0.018f, 10));
        Assert.Equal(404, ex.StatusCode);
    }

    [Fact]
    public void AnUnknownSessionIsNotFoundAndStoppingItIsANoOp()
    {
        Assert.Equal(404, Assert.Throws<ApiException>(() => CalibrationSessionManager.Instance.Get(987654)).StatusCode);
        CalibrationSessionManager.Instance.Stop(987654);
        Assert.Empty(CalibrationSessionManager.Instance.GetAll());
    }
}

public class LegacyCalibrationSinkTests
{
    // Calibration used to be a graph node (SinkType 3 and 6); older data.json files still contain those records.
    [Fact]
    public void LoadDropsLegacyCalibrationSinkRecords()
    {
        var data = new
        {
            Sinks = new object[]
            {
                new { Id = 3, Name = "old camera calibration", Type = 3 },
                new { Id = 4, Name = "old stereo calibration", Type = 6 },
            },
            Sources = Array.Empty<object>(),
        };
        File.WriteAllText("data.json", JsonSerializer.Serialize(data));

        DB.Instance.Load();

        Assert.Empty(SinkManager.Instance.getAllSinkIds());
    }
}
