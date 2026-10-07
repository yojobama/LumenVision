using System.Collections.Generic;

namespace Server
{
    // Per-source detection configuration: everything needed to recreate the one detection sink (ApriltagSink or ObjectDetectionSink) on a Source.
    // SourceManager.ActivateProfile recreates it at the same id so downstream sinks can be re-bound; other sinks on the source are untouched.
    public enum DetectionSinkKind
    {
        ApriltagSink,
        ObjectDetectionSink
    }

    public class PipelineProfile
    {
        // stable 0-based ordinal within this Source, assigned once and never reused; this is what a robot's pipelineIndex refers to
        public int Index { get; set; }
        public string Name { get; set; }
        public DetectionSinkKind Kind { get; set; }

        // --- ApriltagSink settings (Kind == ApriltagSink) ---
        public double? TagSize { get; set; }
        public ApriltagBackendKind? Backend { get; set; }
        public int FrameWidth { get; set; }
        public int FrameHeight { get; set; }
        // detector tuning (see ApriltagTuning in LumenCore/IApriltagBackend.h); null = backend default
        public int? Threads { get; set; }
        public float? QuadDecimate { get; set; }
        public bool? RefineEdges { get; set; }
        // null = REFINE_EXACT (also what profiles saved before the option existed use)
        public RefineEdgesMode? RefineMode { get; set; }
        // family, blur, hamming, decision margin, pose iterations and tag toggles (null = all defaults)
        public ApriltagAdvancedTuning? Advanced { get; set; }
        // path to this profile's own WPILib field-layout JSON; keyed by profile because profiles on one source share the
        // ActiveDetectionSinkId slot, so a sink-id key would let one profile's upload overwrite another's
        public string? FieldLayoutPath { get; set; }
        public bool DriverMode { get; set; }

        // --- ObjectDetectionSink settings (Kind == ObjectDetectionSink) ---
        // a ModelManager-registered model id, as in AddObjectDetectionSink's modelId
        public int? ModelId { get; set; }

        // Camera settings this pipeline applies when activated, on top of the camera's own; null = the pipeline uses the camera's settings as they are.
        public CameraOverrides? CameraOverrides { get; set; }

        public PipelineProfile()
        {
            Name = string.Empty;
        }

        // an independent copy (same index), for duplicating a source with its pipelines
        public PipelineProfile Clone()
        {
            var copy = (PipelineProfile)MemberwiseClone();
            copy.CameraOverrides = CameraOverrides?.Clone();
            copy.Advanced = Advanced?.Clone();
            return copy;
        }
    }
}

namespace Server
{
    // A pipeline's own camera settings (see PipelineProfile.CameraOverrides): generic control values by control id, a frame transform and an FPS limit.
    // A null Transform / FpsLimit keeps the camera's own; controls not listed keep the camera's own too.
    public class CameraOverrides
    {
        public Dictionary<int, int> ControlValues { get; set; } = new Dictionary<int, int>();
        public FrameTransformDto? Transform { get; set; }
        public int? FpsLimit { get; set; }

        public CameraOverrides Clone() => new CameraOverrides
        {
            ControlValues = new Dictionary<int, int>(ControlValues),
            Transform = Transform,
            FpsLimit = FpsLimit,
        };
    }
}
