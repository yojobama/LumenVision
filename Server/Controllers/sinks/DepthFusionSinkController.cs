using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers.sinks
{
    // Fuses a detector's bounding boxes with a StereoDepthSink's depth grid. The detector is bound via PATCH /api/sink/bind
    // (to the StereoDepthSink's rectified-left output); the depth source is attached via /attachDepthSource.
    internal class DepthFusionSinkController : ControllerBase
    {
        // POST: create a DepthFusionSink
        [HttpPost("depthFusionSink/create")]
        public Task<int> Create([FromQuery] string name)
        {
            int sinkId = SinkManager.Instance.AddDepthFusionSink(name);
            return Task.FromResult(sinkId);
        }

        // PATCH: attach the StereoDepthSink whose depth grid this node reads (not an ordinary source bind)
        [HttpPatch("depthFusionSink/{id}/attachDepthSource")]
        public Task AttachDepthSource(int id, [FromQuery] int stereoDepthSinkId)
        {
            SinkManager.Instance.AttachDepthFusionSource(id, stereoDepthSinkId);
            return Task.CompletedTask;
        }
    }
}
