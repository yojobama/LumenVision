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

        // PATCH: retune a live stream: jpegQuality 1-100, scaleDivisor N sends 1/N of the width and height. Remembered across restarts.
        [HttpPatch("mjpegSink/settings")]
        public Task SetSettings([FromQuery] int sinkId, [FromQuery] int jpegQuality = 80, [FromQuery] int scaleDivisor = 1)
        {
            SinkManager.Instance.SetMjpegSettings(sinkId, jpegQuality, scaleDivisor);
            return Task.CompletedTask;
        }
    }
}
