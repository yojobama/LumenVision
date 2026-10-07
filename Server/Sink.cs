using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Server
{
    // Explicit numeric values: SinkType is persisted as a raw integer in data.json, so members must never be reordered or renumbered.
    public enum SinkType
    {
        [Description("ApriltagSink")]
        ApriltagSink = 0,
        [Description("ObjectDetectionSink")]
        ObjectDetectionSink = 1,
        // 2 is reserved; do not reuse.
        // 3 and 6 were calibration sinks, now calibration sessions; they only appear in older saved data, which DB.Load drops. Do not reuse.
        [Description("CameraCalibrationSink")]
        CameraCalibrationSink = 3,
        [Description("NetworkTablesSink")]
        NetworkTablesSink = 4,
        [Description("WebRTCSink")]
        WebRTCSink = 5,
        [Description("StereoCalibrationSink")]
        StereoCalibrationSink = 6,
        [Description("StereoDepthSink")]
        StereoDepthSink = 7,
        [Description("DepthFusionSink")]
        DepthFusionSink = 8,
        [Description("MjpegSink")]
        MjpegSink = 9,
        [Description("RecordSink")]
        RecordSink = 10,
    }

    public class Sink
    {
        private SinkType type { get; set; }
        private int id { get; set; }
        private string name { get; set; }
        
        private Source? source;
        // the right source of a stereo sink (Source holds the left one; see BindStereoSources). Null for non-stereo sinks.
        private Source? source2;


        public SinkType Type
        {
            get => type;
            set
            {
                switch (value)
                {
                    case SinkType.ApriltagSink:
                        id = ManagerWrapper.Instance.CreateApriltagDetector();
                        break;
                    case SinkType.ObjectDetectionSink:
                        // unreachable in practice (see AddSink's no-model overload comment)
                        id = ManagerWrapper.Instance.CreateObjectDetectionSink(ObjectDetectionProvider.ONNX);
                        break;
                }
                type = value;
            }
        }

        public int Id
        {
            get => id;
        }

        public string Name
        {
            get => name;
            set => name = value;
        }
        
        public Source? Source
        {
            get => source;
            set => source = value;
        }

        public Source? Source2
        {
            get => source2;
            set => source2 = value;
        }

        // DepthFusionSink only: the StereoDepthSink id it reads its depth grid from directly (not a Source).
        public int? DepthSourceId { get; set; }

        // RecordSink only: persisted so DB.Load() can recreate it with its original config. Null for other sink types.
        public string? RecordDstFolder { get; set; }
        public string? RecordEncoderName { get; set; }
        public int? RecordBitrateKbps { get; set; }
        public int? RecordSegmentSeconds { get; set; }
        public long? RecordMaxFolderSizeBytes { get; set; }
        public int? RecordMaxFileCount { get; set; }

        // ApriltagSink only: the requested tag size, backend and tuning, persisted for DB.Load() (a Vulkan request is kept after a CPU fallback).
        // Calibration is not persisted here. Null for other sink types and for records saved without these fields.
        public double? ApriltagTagSize { get; set; }
        public ApriltagBackendKind? ApriltagBackend { get; set; }
        public int? ApriltagThreads { get; set; }
        public float? ApriltagQuadDecimate { get; set; }
        public bool? ApriltagRefineEdges { get; set; }
        // which refine-edges implementation the Vulkan backend runs (null in older records: REFINE_EXACT)
        public RefineEdgesMode? ApriltagRefineMode { get; set; }

        // MjpegSink and WebRTCSink: stream tuning, persisted so a restored stream keeps it. Null = the defaults (JPEG quality 80, 4000 kbps, 30 fps, full size).
        public int? StreamJpegQuality { get; set; }
        public int? StreamBitrateKbps { get; set; }
        public int? StreamFps { get; set; }
        public int? StreamScaleDivisor { get; set; }

        // ObjectDetectionSink only: the model the sink was created from, so DB.Load() can rebuild it and a node copy can reuse it.
        public int? ObjectDetectionModelId { get; set; }

        // No `source` constructor parameter: System.Text.Json would bind the JSON "Source" property to it instead of the Source setter.
        public Sink(int id, string name, SinkType type)
        {
            this.id = id;
            this.name = name;
            this.type = type;
        }

        public void ChangeType(SinkType type)
        {
            switch (type)
            {
                case SinkType.ApriltagSink:
                    id = ManagerWrapper.Instance.CreateApriltagDetector();
                    break;
                case SinkType.ObjectDetectionSink:
                    id = ManagerWrapper.Instance.CreateObjectDetectionSink(ObjectDetectionProvider.ONNX);
                    break;
            }
        }
    }
}
