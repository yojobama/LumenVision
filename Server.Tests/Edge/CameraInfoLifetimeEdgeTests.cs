using System;
using System.Linq;
using Server;
using Xunit;

namespace Server.Tests.Edge;

// Found by the live test on the Orange Pi: EnumerateAvailableCameras() returns a native vector whose elements are views into its memory. A camera Source
// kept such a view, so once the garbage collector finalised the vector the next DB.Save or state broadcast read freed memory and aborted the service.
[Collection("ServerSingletons")]
public class CameraInfoLifetimeEdgeTests
{
    [Fact]
    [Trait("Category", "Edge")]
    public void F_ACameraSourceKeepsItsOwnCopyOfTheEnumeratedHardwareInfo()
    {
        var before = SourceManager.Instance.GetAllSourceIds().ToHashSet();
        int id = -1;
        try
        {
            var vector = new VectorCameraHardwareInfo();
            vector.Add(new CameraHardwareInfo { name = "edge lifetime camera", path = "/dev/edge-lifetime-camera" });
            CameraHardwareInfo view = vector[0];
            id = SourceManager.Instance.InitializeCameraSource(view);

            vector.Dispose(); // the native storage the view pointed into is freed
            GC.Collect();
            GC.WaitForPendingFinalizers();
            _ = new byte[1 << 20]; // and reused

            Source source = SourceManager.Instance.GetSourceById(id)!;
            Assert.Equal("/dev/edge-lifetime-camera", source.CameraHardwareInfo!.path);
            Assert.Equal("edge lifetime camera", source.CameraHardwareInfo.name);
        }
        finally
        {
            foreach (int added in SourceManager.Instance.GetAllSourceIds().Where(s => !before.Contains(s)).ToList())
                try { SourceManager.Instance.DeleteSource(added); } catch (Exception) { }
        }
    }
}
