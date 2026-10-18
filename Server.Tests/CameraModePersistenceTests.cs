using System.Text.Json;
using Server;
using Xunit;

namespace Server.Tests;

public class CameraModePersistenceTests
{
    [Fact]
    public void ACameraRecordKeepsTheCaptureModeItWasSetTo()
    {
        var camera = new Source(5, "cam", SourceType.Camera, cameraHardwareInfo: new CameraHardwareInfo { name = "UVC", path = "/dev/v4l/by-path/x" })
        {
            Mode = new CameraModeDto(1280, 720, 30.0, FrameFormat.MJPEG, false),
        };

        var restored = JsonSerializer.Deserialize<Source>(JsonSerializer.Serialize(camera));

        Assert.Equal(new CameraModeDto(1280, 720, 30.0, FrameFormat.MJPEG, false), restored!.Mode);
    }

    [Fact]
    public void RecordsSavedBeforeTheModeWasRememberedLoadWithNoMode()
    {
        const string oldRecord = """{"Type":0,"Id":5,"Name":"cam","Profiles":[],"ActiveProfileIndex":-1}""";

        var restored = JsonSerializer.Deserialize<Source>(oldRecord);

        Assert.Null(restored!.Mode);
    }

    [Fact]
    public void ChangingTheModeOfAnUnknownSourceIs404()
    {
        var ex = Assert.Throws<Server.Web.ApiException>(() => SourceManager.Instance.SetCameraMode(987654, new CameraModeDto(640, 480, 30, FrameFormat.MJPEG, true)));
        Assert.Equal(404, ex.StatusCode);
    }
}
