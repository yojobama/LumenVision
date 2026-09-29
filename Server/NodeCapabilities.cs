using System.Collections.Generic;
using System.Linq;

namespace Server
{
    // Static, hand-authored manifest of node types and wiring rules for the webui's graph editor; the rules live across
    // SinkManager, ISink::maxSources and IStereoRoleReceiver with no reflectable source, so it is kept in sync manually.
    public record struct NodeTypeCapability(
        string TypeName,
        string Category,              // "source" | "sink"
        string DisplayName,
        string Icon,                  // lucide-react icon name
        int MaxSources,               // 0 for a real (camera/file) source - it has nothing bound to it
        string[]? SourceRoles,        // null = a single unlabeled bind; ["left","right"] for stereo nodes
        bool IsDualRoleSink,          // can itself be bound as another sink's source (its own detection/
                                       // fusion output) - see SinkManager.DualRoleSinkTypes
        bool HasDepthAttach,          // DepthFusionSink only - a second, non-Source input via
                                       // AttachDepthFusionSource, not an ordinary bind
        bool Implemented              // false for a node type with no working creation path
    );

    public static class NodeCapabilities
    {
        // IsDualRoleSink mirrors SinkManager.DualRoleSinkTypes by hand (kept dependency-free); keep both in sync.

        public static readonly IReadOnlyList<NodeTypeCapability> Sources = new List<NodeTypeCapability>
        {
            new("Camera", "source", "Camera", "camera", 0, null, false, false, true),
            new("ImageFile", "source", "Image File", "image", 0, null, false, false, true),
            new("VideoFile", "source", "Video File", "video", 0, null, false, false, true),
            // SinkOutput is synthetic (a dual-role sink acting as its own source) and never created directly, so it is omitted
        };

        public static readonly IReadOnlyList<NodeTypeCapability> Sinks = new List<NodeTypeCapability>
        {
            new("ApriltagSink", "sink", "AprilTag Detector", "scan", 1, null, true, false, true),
            new("ObjectDetectionSink", "sink", "Object Detection", "box", 1, null, true, false, true),
            new("NetworkTablesSink", "sink", "NetworkTables", "radio", 1, null, false, false, true),
            new("WebRTCSink", "sink", "WebRTC Preview", "video", 1, null, false, false, true),
            new("MjpegSink", "sink", "MJPEG Preview", "video", 1, null, false, false, true),
            new("RecordSink", "sink", "Recording", "film", 1, null, false, false, true),
            new("StereoDepthSink", "sink", "Stereo Depth", "layers", 2, new[] { "left", "right" }, true, false, true),
            new("DepthFusionSink", "sink", "Depth Fusion", "combine", 1, null, true, true, true),
        };

        public static NodeTypeCapability? FindSink(SinkType type) =>
            Sinks.FirstOrDefault(c => c.TypeName == type.ToString());

        public static NodeTypeCapability? FindSource(SourceType type) =>
            Sources.FirstOrDefault(c => c.TypeName == type.ToString());
    }
}
