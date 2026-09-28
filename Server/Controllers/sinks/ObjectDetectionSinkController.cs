using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers.sinks
{
    internal class ObjectDetectionSinkController : ControllerBase
    {
        // POST: create an object detection sink for an uploaded model; the backend (ONNX or RKNN) follows from the model.
        [HttpPost("objectDetectionSink/create")]
        public Task<int> Create([FromQuery] string name, [FromQuery] int modelId)
        {
            int sinkId = SinkManager.Instance.AddObjectDetectionSink(name, modelId);
            return Task.FromResult(sinkId);
        }

        // GET: the backend the sink is running (informational; it cannot be switched)
        [HttpGet("objectDetectionSink/backend")]
        public Task<string> GetBackend([FromQuery] int sinkId)
        {
            return Task.FromResult(SinkManager.Instance.GetObjectDetectionSinkBackendName(sinkId));
        }
    }
}
