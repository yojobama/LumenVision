using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers.sinks
{
    internal class ObjectDetectionSinkController : ControllerBase
    {
        // POST: create an object detection sink for an uploaded model; the backend (ONNX or RKNN) follows from the model.
        [HttpPost("objectDetectionSink/create")]
        public Task<int> Create([FromQuery] string name, [FromQuery] int modelId, [FromQuery] float? confThreshold = null,
            [FromQuery] float? nmsThreshold = null)
        {
            int sinkId = SinkManager.Instance.AddObjectDetectionSink(name, modelId, confThreshold, nmsThreshold);
            return Task.FromResult(sinkId);
        }

        // GET: the confidence/NMS cutoffs the sink's model is running with, and the model it was created from
        [HttpGet("objectDetectionSink/thresholds")]
        public Task<ObjectDetectionThresholdsDto> GetThresholds([FromQuery] int sinkId)
        {
            var (conf, nms, modelId) = SinkManager.Instance.GetObjectDetectionThresholds(sinkId);
            return Task.FromResult(new ObjectDetectionThresholdsDto(conf, nms, modelId));
        }

        // PATCH: retune a running sink (0.01-1 each); remembered on the sink, overriding its model's defaults
        [HttpPatch("objectDetectionSink/thresholds")]
        public Task SetThresholds([FromQuery] int sinkId, [FromQuery] float confThreshold, [FromQuery] float nmsThreshold)
        {
            SinkManager.Instance.SetObjectDetectionThresholds(sinkId, confThreshold, nmsThreshold);
            return Task.CompletedTask;
        }

        // GET: the backend the sink is running (informational; it cannot be switched)
        [HttpGet("objectDetectionSink/backend")]
        public Task<string> GetBackend([FromQuery] int sinkId)
        {
            return Task.FromResult(SinkManager.Instance.GetObjectDetectionSinkBackendName(sinkId));
        }
    }
}
