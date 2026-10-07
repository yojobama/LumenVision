using System;
using Server.Controllers;
using Server.Controllers.sinks;
using Server.Controllers.sources;

namespace Server
{
    // The controller types that are registered: Program.cs registers this list and
    // Server/OpenApi/OpenApiGenerator.cs documents it.
    public static class RegisteredControllers
    {
        public static readonly Type[] All =
        {
            // sinks
            typeof(SinkController),
            typeof(ApriltagSinkController),
            typeof(NetworkTablesSinkController),
            typeof(ObjectDetectionSinkController),
            typeof(WebRTCSinkController),
            typeof(StereoDepthSinkController),
            typeof(DepthFusionSinkController),
            typeof(MjpegSinkController),
            typeof(RecordSinkController),
            // sources
            typeof(SourceController),
            typeof(ImageFileSourceController),
            typeof(VideoFileSourceController),
            typeof(CameraSourceController),
            typeof(PipelineProfileController),
            // models
            typeof(ModelController),
            // others
            typeof(DeviceController),
            typeof(DeviceSettingsController),
            typeof(CapabilitiesController),
            typeof(CalibrationController),
            typeof(OpenApiController),
            typeof(LogController),
            typeof(SnapshotController),
            typeof(GraphProfileController),
        };
    }
}
