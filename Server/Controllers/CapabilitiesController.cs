using System.Linq;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers
{
    // Reports build features and node-type wiring rules so the webui can disable and validate options.
    internal class CapabilitiesController : ControllerBase
    {
        // GET: LUMEN_WITH_* features this build was compiled with (e.g. "ONNX", "NT4", "WEBRTC",
        // "VULKAN_APRILTAG", "CODEC_STEREO", "RKNN").
        [HttpGet("capabilities/features")]
        public Task<string[]> GetEnabledFeatures()
        {
            return Task.FromResult(ManagerWrapper.Instance.GetEnabledFeatures().ToArray());
        }

        // GET: every creatable source/sink node type with its wiring rules (see NodeCapabilities.cs).
        [HttpGet("capabilities/nodeTypes")]
        public Task<NodeTypesResponse> GetNodeTypes()
        {
            return Task.FromResult(new NodeTypesResponse(NodeCapabilities.Sources.ToArray(), NodeCapabilities.Sinks.ToArray()));
        }
    }

    public record struct NodeTypesResponse(NodeTypeCapability[] Sources, NodeTypeCapability[] Sinks);
}
