using System;
using System.IO;
using Server;
using Xunit;

namespace Server.Tests;

[Collection("ServerSingletons")]
public class FieldLayoutPersistenceTests
{
    private const string TwoTagLayout =
        "{\"tags\":[{\"ID\":3,\"pose\":{\"translation\":{\"x\":4.0,\"y\":2.0,\"z\":0.5},\"rotation\":{\"quaternion\":{\"W\":1.0,\"X\":0.0,\"Y\":0.0,\"Z\":0.0}}}},"
        + "{\"ID\":4,\"pose\":{\"translation\":{\"x\":4.0,\"y\":2.5,\"z\":0.5},\"rotation\":{\"quaternion\":{\"W\":1.0,\"X\":0.0,\"Y\":0.0,\"Z\":0.0}}}}],\"field\":{\"length\":16.5,\"width\":8.2}}";

    [Fact]
    public void ADetectorKeepsItsFieldLayoutWhenItIsRebuiltForNewTuningOrAnotherBackend()
    {
        int id = SinkManager.Instance.AddApriltagSinkWithBackend("tags", 0.1651, ApriltagBackendKind.APRILTAG_BACKEND_CPU, 0, 0);
        string directory = Path.Combine(AppContext.BaseDirectory, "fieldLayouts");
        Directory.CreateDirectory(directory);
        string path = Path.Combine(directory, $"sink-{id}.json");
        try
        {
            File.WriteAllText(path, TwoTagLayout);
            Assert.True(ManagerWrapper.Instance.LoadFieldLayout(id, path));
            Assert.Equal(2, ManagerWrapper.Instance.GetFieldLayoutTagCount(id));

            // Apply tuning and a backend switch both rebuild the native detector at the same id
            SinkManager.Instance.SetApriltagBackend(id, ApriltagBackendKind.APRILTAG_BACKEND_CPU, nthreads: 2);
            Assert.Equal(2, ManagerWrapper.Instance.GetFieldLayoutTagCount(id));

            SinkManager.Instance.SetApriltagBackend(id, ApriltagBackendKind.APRILTAG_BACKEND_VULKAN);
            Assert.Equal(2, ManagerWrapper.Instance.GetFieldLayoutTagCount(id));
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
            File.Delete(path);
        }
    }

    [Fact]
    public void ADetectorMadeWithoutACalibrationSaysSo()
    {
        int id = SinkManager.Instance.AddApriltagSinkWithBackend("tags", 0.1651, ApriltagBackendKind.APRILTAG_BACKEND_CPU, 0, 0);
        try
        {
            Assert.False(ManagerWrapper.Instance.GetApriltagDetectorHasCalibration(id));
            Assert.True(ManagerWrapper.Instance.SetApriltagDetectorCalibration(id, new CameraCalibrationResult(700, 700, 320, 240, 0.2, new VectorDouble(), 640, 480)));
            Assert.True(ManagerWrapper.Instance.GetApriltagDetectorHasCalibration(id));
            Assert.False(ManagerWrapper.Instance.SetApriltagDetectorCalibration(987654, new CameraCalibrationResult()));
        }
        finally
        {
            SinkManager.Instance.DeleteSink(id);
        }
    }
}
