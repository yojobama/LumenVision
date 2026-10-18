using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Server
{
    public enum SourceType
    {
        Camera,
        ImageFile,
        VideoFile,
        // A dual-role sink (ApriltagSink, ObjectDetectionSink) acting as its own frame/json-producing source;
        // natively in Manager's m_Sources but not tracked by SourceManager. See SinkManager.BindSourceToSink.
        SinkOutput,
        // Part of another source's frame (one half of a side-by-side stereo camera, say), published as a source of its own: CropParentId and
        // CropRegion/CropLayout say which. Natively a RoiSource, which is both a sink of the parent and a source for what binds to it.
        Crop
    }

    public class Source
    {
        private int id;
        private string name;
        private SourceType type;
        
        public CameraHardwareInfo? CameraHardwareInfo { get; set; } // Optional camera hardware info for camera sources
        public int? Fps { get; set; } // Optional frames per second for camera sources
        public string? FilePath { get; set; } // Optional file path for image or video sources

        // Pipeline profiles (see PipelineProfile.cs). ActiveDetectionSinkId is the stable detection sink id reused across profile activations;
        // ActiveProfileIndex is -1 until a profile is activated.
        public List<PipelineProfile> Profiles { get; set; } = new List<PipelineProfile>();
        public int ActiveProfileIndex { get; set; } = -1;
        public int? ActiveDetectionSinkId { get; set; }

        // results per second this source publishes at most; null = unlimited (see ISource::SetFpsLimit)
        public int? FpsLimit { get; set; }

        // generic camera control values the user has set (control id -> value), reapplied when the source is restored
        public Dictionary<int, int>? ControlValues { get; set; }

        // the capture mode (resolution, frame rate, pixel format) last requested for this camera and accepted by the device, reapplied when the
        // source is restored; null = never chosen, so the camera opens in its default mode (often 640x480)
        public CameraModeDto? Mode { get; set; }

        // Crop sources only: the source this one republishes part of, the region it keeps in the parent's pixels, and, for the standard splits,
        // which half it is ("left", "right", "top", "bottom"; null = a custom region). A split follows its camera when the camera's resolution changes.
        public int? CropParentId { get; set; }
        public CropRegionDto? CropRegion { get; set; }
        public string? CropLayout { get; set; }

        // crop/rotate/mirror applied to every frame this camera publishes; null = none
        public FrameTransformDto? Transform { get; set; }

        public SourceType Type
        {
            get { return type; }
        }

        public int Id
        {
            get => id; set => id = value;
        }
        
        public string Name
        {
            get => name;
            set => name = value;
        }

        public Source(int id, string name, SourceType type, string? filePath = null, int? fps = null, CameraHardwareInfo? cameraHardwareInfo = null)
        {
            this.id = id;
            this.name = name;
            this.type = type;
            this.FilePath = filePath;
            this.Fps = fps;
            this.CameraHardwareInfo = cameraHardwareInfo;
        }
    }
}
