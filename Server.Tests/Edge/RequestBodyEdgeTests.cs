using System.IO;
using System.Text;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Server;
using Server.Controllers.sinks;
using Server.Controllers.sources;
using Server.Web;
using Xunit;

namespace Server.Tests.Edge;

// Found by the live test on the Orange Pi: PATCH cameraSource/{id}/mode with no body answered 500, because the controller deserialised the raw body
// itself. A request that is wrong in this way is a 400, whichever call it is.
public class RequestBodyEdgeTests
{
    private static ControllerContext ContextWith(string body)
    {
        var context = new DefaultHttpContext();
        context.Request.Body = new MemoryStream(Encoding.UTF8.GetBytes(body));
        return new ControllerContext { HttpContext = context };
    }

    public static TheoryData<string> BadBodies => new() { "", "   ", "null", "{not json", "[1,2,3]", "\"text\"" };

    [Theory]
    [Trait("Category", "Edge")]
    [MemberData(nameof(BadBodies))]
    public async Task F_AMalformedBodyIsA400OnEveryCallThatReadsOne(string body)
    {
        var camera = new CameraSourceController { ControllerContext = ContextWith(body) };
        await AssertBadRequest(() => camera.SetMode(1));

        camera = new CameraSourceController { ControllerContext = ContextWith(body) };
        await AssertBadRequest(() => camera.SetTransform(1));

        camera = new CameraSourceController { ControllerContext = ContextWith(body) };
        await AssertBadRequest(() => camera.Create());

        var depth = new StereoDepthSinkController { ControllerContext = ContextWith(body) };
        await AssertBadRequest(() => depth.Create("depth", StereoDepthBackendKind.STEREO_BACKEND_SGBM, 0.5, 10, 0, default));
    }

    private static async Task AssertBadRequest(System.Func<Task> call)
    {
        System.Exception? error = await Record.ExceptionAsync(call);
        Assert.Equal(400, Assert.IsType<ApiException>(error).StatusCode);
    }
}
