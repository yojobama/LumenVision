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
        // A dual-role sink (ApriltagSink, ObjectDetectionSink, CameraCalibrationSink) acting as its own frame/json-producing source;
        // natively in Manager's m_Sources but not tracked by SourceManager. See SinkManager.BindSourceToSink.
        SinkOutput
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
