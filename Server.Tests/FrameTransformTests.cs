using System.Text.Json;
using Server;
using Xunit;

namespace Server.Tests;

[Collection("ServerSingletons")]
public class FrameTransformTests
{
    [Fact]
    public void DefaultTransformIsIdentityAndEverythingElseIsNot()
    {
        Assert.True(default(FrameTransformDto).IsIdentity);
        Assert.True(new FrameTransformDto(360, false, false, 0, 0, 0, 0).IsIdentity);
        Assert.False(new FrameTransformDto(90, false, false, 0, 0, 0, 0).IsIdentity);
        Assert.False(new FrameTransformDto(0, true, false, 0, 0, 0, 0).IsIdentity);
        Assert.False(new FrameTransformDto(0, false, false, 10, 10, 100, 80).IsIdentity);
    }

    [Fact]
    public void SourceRecordKeepsItsTransformAndOldRecordsLoadWithNone()
    {
        var source = new Source(4, "front", SourceType.Camera) { Transform = new FrameTransformDto(270, true, false, 0, 0, 0, 0) };

        var restored = JsonSerializer.Deserialize<Source>(JsonSerializer.Serialize(source));

        Assert.Equal(270, restored!.Transform!.Value.Rotation);
        Assert.True(restored.Transform.Value.FlipHorizontal);
        Assert.Null(JsonSerializer.Deserialize<Source>("""{"Id":4,"Name":"front"}""")!.Transform);
    }

    [Fact]
    public void ACalibrationIsCarriedAcrossARotationByTheNativeLayer()
    {
        var distCoeffs = new VectorDouble { -0.2, 0.05, 0.003, -0.002, 0.01 };
        var calibration = new CameraCalibrationResult(150, 140, 82, 58.5, 0.3, distCoeffs, 160, 120);

        CameraCalibrationResult rotated = ManagerWrapper.Instance.TransformCameraCalibration(
            calibration, new FrameTransformDto(90, false, false, 0, 0, 0, 0).ToNative());

        Assert.Equal(120, rotated.imageWidth);
        Assert.Equal(160, rotated.imageHeight);
        Assert.Equal(140, rotated.fx);
        Assert.Equal(150, rotated.fy);
        Assert.Equal(120 - 1 - 58.5, rotated.cx);
        Assert.Equal(82, rotated.cy);
    }
}
