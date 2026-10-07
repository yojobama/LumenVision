using System;
using System.Linq;

namespace Server
{
    // Plain, OpenAPI-friendly shapes for endpoints that would otherwise serialise SWIG types (live P/Invoke getters, raw C++ field names).
    // Mapped once at the controller boundary.

    public record struct CameraModeDto(int Width, int Height, double Fps, FrameFormat PixelFormat, bool IsNative)
    {
        public static CameraModeDto From(CameraMode mode) =>
            new(mode.width, mode.height, mode.fps, mode.pixelFormat, mode.isNative);

        public CameraMode ToNative()
        {
            var mode = new CameraMode
            {
                width = Width,
                height = Height,
                fps = Fps,
                pixelFormat = PixelFormat,
                isNative = IsNative
            };
            return mode;
        }
    }

    public record struct CameraHardwareInfoDto(string Name, string Path)
    {
        public static CameraHardwareInfoDto From(CameraHardwareInfo info) => new(info.name, info.path);

        public CameraHardwareInfo ToNative() => new() { name = Name, path = Path };
    }

    // a camera control's real device range (LumenCore's CameraControlRange) - Supported=false
    // means the camera has no such control and every other field is meaningless
    public record struct CameraControlRangeDto(bool Supported, int Minimum, int Maximum, int Step, int Default, int Value)
    {
        public static CameraControlRangeDto From(CameraControlRange range) =>
            new(range.supported, range.minimum, range.maximum, range.step, range.defaultValue, range.value);
    }

    // a camera's frame transform (LumenCore's FrameTransform): crop in the camera's own pixels (0 width/height = none), then a clockwise
    // rotation of 0/90/180/270 degrees, then mirrors
    public record struct FrameTransformDto(int Rotation, bool FlipHorizontal, bool FlipVertical, int CropX, int CropY, int CropWidth, int CropHeight)
    {
        public bool IsIdentity => Rotation % 360 == 0 && !FlipHorizontal && !FlipVertical && CropWidth <= 0 && CropHeight <= 0;

        public static FrameTransformDto From(FrameTransform t) =>
            new(t.rotation, t.flipHorizontal, t.flipVertical, t.cropX, t.cropY, t.cropWidth, t.cropHeight);

        public FrameTransform ToNative() => new()
        {
            rotation = Rotation, flipHorizontal = FlipHorizontal, flipVertical = FlipVertical,
            cropX = CropX, cropY = CropY, cropWidth = CropWidth, cropHeight = CropHeight,
        };
    }

    public record struct ObjectDetectionThresholdsDto(float ConfThreshold, float NmsThreshold, int? ModelId);

    public record struct CameraControlsDto(CameraControlRangeDto Exposure, CameraControlRangeDto Gain);

    // one generic camera control (LumenCore's CameraControlInfo); Kind is 0 integer, 1 boolean, 2 menu (MenuValues/MenuLabels) or 3 button
    public record struct CameraControlDto(int Id, string Name, int Kind, int Minimum, int Maximum, int Step, int Default, int Value,
        bool ReadOnly, bool Inactive, string[] MenuLabels, int[] MenuValues)
    {
        public static CameraControlDto From(CameraControlInfo info) => new(info.id, info.name, (int)info.kind, info.minimum, info.maximum,
            info.step, info.defaultValue, info.value, info.readOnly, info.inactive, info.menuLabels.ToArray(), info.menuValues.ToArray());
    }

    public record struct CameraCalibrationResultDto(
        double Fx, double Fy, double Cx, double Cy, double Rms,
        double[] DistCoeffs, int ImageWidth, int ImageHeight)
    {
        public static CameraCalibrationResultDto From(CameraCalibrationResult result) => new(
            result.fx, result.fy, result.cx, result.cy, result.rms,
            result.distCoeffs.ToArray(), result.imageWidth, result.imageHeight);

        public CameraCalibrationResult ToNative()
        {
            var distCoeffs = new VectorDouble();
            foreach (double d in DistCoeffs) distCoeffs.Add(d);
            return new CameraCalibrationResult(Fx, Fy, Cx, Cy, Rms, distCoeffs, ImageWidth, ImageHeight);
        }
    }

    public record struct StoredCalibrationDto(string CameraPath, CameraCalibrationResultDto Result, long CalibratedAtUnixMs)
    {
        // Independent of StoredCalibration's on-disk shape (calibrations.json keeps the raw SWIG field names); this DTO exists only at the REST boundary.
        public static StoredCalibrationDto From(StoredCalibration stored) =>
            new(stored.CameraPath, CameraCalibrationResultDto.From(stored.Result), stored.CalibratedAtUnixMs);
    }

    public record struct StoredStereoCalibrationDto(string LeftCameraPath, string RightCameraPath, StereoCalibrationResultDto Result, long CalibratedAtUnixMs)
    {
        public static StoredStereoCalibrationDto From(StoredStereoCalibration stored) =>
            new(stored.LeftCameraPath, stored.RightCameraPath, StereoCalibrationResultDto.From(stored.Result), stored.CalibratedAtUnixMs);
    }

    // A running calibration session; PreviewSinkId is an MJPEG sink streamed from /stream/mjpeg?SinkID=.
    public record struct CalibrationSessionDto(int SessionId, string Kind, int PreviewSinkId)
    {
        public static CalibrationSessionDto From(CalibrationSession session) =>
            new(session.Id, session.Kind.ToString().ToLowerInvariant(), session.PreviewSinkId);
    }

    public record struct CalibrationStatusDto(bool HasCalibration, bool MatchesCurrentResolution, int? CalibratedWidth, int? CalibratedHeight)
    {
        public static CalibrationStatusDto From(CalibrationStatus status) =>
            new(status.HasCalibration, status.MatchesCurrentResolution, status.CalibratedWidth, status.CalibratedHeight);
    }

    public record struct StereoCalibrationResultDto(
        CameraCalibrationResultDto Left, CameraCalibrationResultDto Right,
        double[] R, double[] T, double[] E, double[] F,
        double[] R1, double[] R2, double[] P1, double[] P2, double[] Q,
        double StereoRms, double EpipolarRms, double BaselineMeters,
        double RectifiedFx, double RectifiedCx, double RectifiedCy,
        int ImageWidth, int ImageHeight,
        int RoiLeftX, int RoiLeftY, int RoiLeftW, int RoiLeftH,
        int RoiRightX, int RoiRightY, int RoiRightW, int RoiRightH)
    {
        public static StereoCalibrationResultDto From(StereoCalibrationResult r) => new(
            CameraCalibrationResultDto.From(r.left), CameraCalibrationResultDto.From(r.right),
            r.R.ToArray(), r.T.ToArray(), r.E.ToArray(), r.F.ToArray(),
            r.R1.ToArray(), r.R2.ToArray(), r.P1.ToArray(), r.P2.ToArray(), r.Q.ToArray(),
            r.stereoRms, r.epipolarRms, r.baselineMeters,
            r.rectifiedFx, r.rectifiedCx, r.rectifiedCy,
            r.imageWidth, r.imageHeight,
            r.roiLeftX, r.roiLeftY, r.roiLeftW, r.roiLeftH,
            r.roiRightX, r.roiRightY, r.roiRightW, r.roiRightH);

        private static VectorDouble Vec(double[] values)
        {
            var v = new VectorDouble();
            foreach (double d in values) v.Add(d);
            return v;
        }

        public StereoCalibrationResult ToNative() => new()
        {
            left = Left.ToNative(),
            right = Right.ToNative(),
            R = Vec(R),
            T = Vec(T),
            E = Vec(E),
            F = Vec(F),
            R1 = Vec(R1),
            R2 = Vec(R2),
            P1 = Vec(P1),
            P2 = Vec(P2),
            Q = Vec(Q),
            stereoRms = StereoRms,
            epipolarRms = EpipolarRms,
            baselineMeters = BaselineMeters,
            rectifiedFx = RectifiedFx,
            rectifiedCx = RectifiedCx,
            rectifiedCy = RectifiedCy,
            imageWidth = ImageWidth,
            imageHeight = ImageHeight,
            roiLeftX = RoiLeftX,
            roiLeftY = RoiLeftY,
            roiLeftW = RoiLeftW,
            roiLeftH = RoiLeftH,
            roiRightX = RoiRightX,
            roiRightY = RoiRightY,
            roiRightW = RoiRightW,
            roiRightH = RoiRightH
        };
    }

    public record struct StereoDepthStatsDto(double ValidFraction, double MedianDepthMeters);

    // Threads/QuadDecimate are user-adjustable; QuadDecimateSupported is false for Vulkan (fixed 2x decimation),
    // so the Inspector disables that control.
    // RefineModeSupported is false on the CPU backend, which always runs upstream's refine_edges.
    // Family .. SingleTagPose are the settings in effect (see ApriltagAdvancedTuning); QuadSigmaSupported is false on the Vulkan backend (no blur stage).
    public record struct ApriltagTuningDto(int Threads, float QuadDecimate, bool QuadDecimateSupported, bool RefineEdges,
        RefineEdgesMode RefineMode, bool RefineModeSupported, ApriltagFamilyKind Family, float QuadSigma, bool QuadSigmaSupported,
        int MaxHamming, float DecisionMargin, int PoseIterations, bool MultiTag, bool SingleTagPose);

    // Every saved snapshot/pair's detected corners for the calibration coverage heatmap;
    // each Snapshots entry is one snapshot flattened as [x0,y0,x1,y1,...].
    public record struct CalibrationCoverageDto(int FrameWidth, int FrameHeight, double[][] Snapshots);

    // NetworkTablesSink/WebRTCSink::GetConnectionStatus() return JSON as a std::string; it is parsed once here into a typed object.
    public record struct NetworkTablesStatusDto(bool Connected, string Identity, string RootTable, int? TeamNumber, string? ServerAddress)
    {
        public static NetworkTablesStatusDto Parse(string json)
        {
            using var doc = System.Text.Json.JsonDocument.Parse(json);
            var root = doc.RootElement;
            return new NetworkTablesStatusDto(
                root.GetProperty("connected").GetBoolean(),
                root.GetProperty("identity").GetString() ?? "",
                root.GetProperty("rootTable").GetString() ?? "",
                root.TryGetProperty("teamNumber", out var tn) ? tn.GetInt32() : null,
                root.TryGetProperty("serverAddress", out var sa) ? sa.GetString() : null);
        }
    }

    public record struct WebRtcStatusDto(bool Connected, int IceState, bool GatheringComplete)
    {
        public static WebRtcStatusDto Parse(string json)
        {
            using var doc = System.Text.Json.JsonDocument.Parse(json);
            var root = doc.RootElement;
            return new WebRtcStatusDto(
                root.GetProperty("connected").GetBoolean(),
                root.GetProperty("iceState").GetInt32(),
                root.GetProperty("gatheringComplete").GetBoolean());
        }
    }
}
