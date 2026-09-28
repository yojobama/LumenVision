using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers.sinks
{
    // Creation only; the stream itself is served by MjpegStreamModule.cs.
    internal class MjpegSinkController : ControllerBase
    {
        // POST: create an MjpegSink; bind it afterwards (PATCH /sink/bind) to the node whose frames are streamed.
        // jpegQuality is nullable and defaults to 80 in the body, so an omitted query key never becomes quality 0.
        [HttpPost("mjpegSink/create")]
        public Task<int> Create([FromQuery] string name, [FromQuery] int? jpegQuality = null)
        {
            int sinkId = SinkManager.Instance.AddMjpegSink(name, jpegQuality ?? 80);
            return Task.FromResult(sinkId);
        }
    }
}
