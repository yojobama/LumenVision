using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers.sinks
{
    internal class ApriltagSinkController : ControllerBase
    {
        // POST: Create an Apriltag sink
        [HttpPost("apriltagSink/create")]
        public Task<int> Create([FromQuery] string name, [FromQuery] string type)
        {
            int SinkID = SinkManager.Instance.AddSink(name, type);
            DB.Instance.Save();
            return Task.FromResult(SinkID);
        }

        // POST: create an Apriltag sink using a camera source's saved calibration at its current resolution,
        // so detected tags' real-world location can be computed
        [HttpPost("apriltagSink/createFromCamera")]
        public Task<int> CreateFromCamera([FromQuery] string name, [FromQuery] int sourceId, [FromQuery] double tagSize)
        {
            int sinkId = SinkManager.Instance.AddApriltagSinkForCamera(name, sourceId, tagSize);
            return Task.FromResult(sinkId);
        }

        // POST: create an Apriltag sink with an explicit backend (cpu/vulkan) and no calibration.
        // frameWidth/frameHeight/nthreads/quadDecimate/refineEdges/refineMode are optional; omitted means backend default.
        [HttpPost("apriltagSink/createWithBackend")]
        public Task<int> CreateWithBackend([FromQuery] string name, [FromQuery] double tagSize,
            [FromQuery] ApriltagBackendKind backend, [FromQuery] int frameWidth = 0, [FromQuery] int frameHeight = 0,
            [FromQuery] int? nthreads = null, [FromQuery] float? quadDecimate = null, [FromQuery] bool? refineEdges = null,
            [FromQuery] RefineEdgesMode? refineMode = null)
        {
            int sinkId = SinkManager.Instance.AddApriltagSinkWithBackend(name, tagSize, backend, frameWidth, frameHeight,
                nthreads ?? 0, quadDecimate ?? 0.0f, refineEdges ?? true, refineMode ?? RefineEdgesMode.REFINE_EXACT);
            return Task.FromResult(sinkId);
        }

        // GET: the backend the sink is actually running (Vulkan falls back to CPU without a usable device)
        [HttpGet("apriltagSink/backend")]
        public Task<string> GetBackend([FromQuery] int sinkId)
        {
            return Task.FromResult(SinkManager.Instance.GetApriltagBackendName(sinkId));
        }

        // GET: the backend as its enum value rather than a display string.
        [HttpGet("apriltagSink/backendKind")]
        public Task<ApriltagBackendKind> GetBackendKind([FromQuery] int sinkId)
        {
            return Task.FromResult(ManagerWrapper.Instance.GetApriltagDetectorBackendKind(sinkId));
        }

        // GET: the sink's effective tuning (threads, quad_decimate, refine_edges and its method); on Vulkan QuadDecimate is
        // the integer the GPU pipeline runs, which may differ from the request.
        [HttpGet("apriltagSink/tuning")]
        public Task<ApriltagTuningDto> GetTuning([FromQuery] int sinkId)
        {
            return Task.FromResult(new ApriltagTuningDto
            {
                Threads = ManagerWrapper.Instance.GetApriltagDetectorThreads(sinkId),
                QuadDecimate = ManagerWrapper.Instance.GetApriltagDetectorQuadDecimate(sinkId),
                QuadDecimateSupported = ManagerWrapper.Instance.GetApriltagDetectorQuadDecimateSupported(sinkId),
                RefineEdges = ManagerWrapper.Instance.GetApriltagDetectorRefineEdges(sinkId),
                RefineMode = ManagerWrapper.Instance.GetApriltagDetectorRefineMode(sinkId),
                RefineModeSupported = ManagerWrapper.Instance.GetApriltagDetectorRefineModeSupported(sinkId),
            });
        }

        // PATCH: switch an existing sink between CPU/Vulkan in place, keeping its id, settings and bindings.
        // nthreads/quadDecimate/refineEdges/refineMode are optional; when omitted the current tuning is carried over.
        [HttpPatch("apriltagSink/backend")]
        public Task SetBackend([FromQuery] int sinkId, [FromQuery] ApriltagBackendKind backend,
            [FromQuery] int? nthreads = null, [FromQuery] float? quadDecimate = null, [FromQuery] bool? refineEdges = null,
            [FromQuery] RefineEdgesMode? refineMode = null)
        {
            SinkManager.Instance.SetApriltagBackend(sinkId, backend, nthreads, quadDecimate, refineEdges, refineMode);
            return Task.CompletedTask;
        }


        // POST: upload a WPILib AprilTagFieldLayout JSON body and enable multi-tag PnP on this sink.
        // Returns the number of tags loaded, or -1 if the body is not a valid field layout.
        [HttpPost("apriltagSink/fieldLayout")]
        public async Task<int> SetFieldLayout([FromQuery] int sinkId)
        {
            using var reader = new StreamReader(HttpContext.OpenRequestStream());
            string json = await reader.ReadToEndAsync();

            string layoutDir = Path.Combine(AppContext.BaseDirectory, "fieldLayouts");
            Directory.CreateDirectory(layoutDir);
            string path = Path.Combine(layoutDir, $"sink-{sinkId}.json");
            await System.IO.File.WriteAllTextAsync(path, json);

            bool ok = ManagerWrapper.Instance.LoadFieldLayout(sinkId, path);
            return ok ? ManagerWrapper.Instance.GetFieldLayoutTagCount(sinkId) : -1;
        }

        // GET: how many tags this sink's currently-loaded field layout has (0 if none loaded)
        [HttpGet("apriltagSink/fieldLayoutTagCount")]
        public Task<int> GetFieldLayoutTagCount([FromQuery] int sinkId)
        {
            return Task.FromResult(ManagerWrapper.Instance.GetFieldLayoutTagCount(sinkId));
        }
    }
}
