// Types
export interface Source {
  id: number;
  name: string;
  type: string;
  status: 'active' | 'inactive' | 'error';
  lastUpdate?: Date;
  filePath?: string; // For video/image file sources
  fps?: number; // For video file sources
  cameraHardwareInfo?: CameraHardwareInfo; // For camera sources
}

export interface Sink {
  id: number;
  name: string;
  type: string;
  status: 'active' | 'inactive' | 'error';
  isStreaming?: boolean;
  lastUpdate?: Date;
  sourceId?: number;
  // stereo sinks only: sourceId is the LEFT camera, this is the RIGHT one
  source2Id?: number;
  isEnabled?: boolean; // Track whether sink is enabled/disabled
}

// mirrors Server/Dtos.cs's CameraHardwareInfoDto (PascalCase)
export interface CameraHardwareInfo {
  Name: string;
  Path: string;
}

// mirrors Server/Dtos.cs's CameraModeDto (PascalCase)
// PixelFormat is FrameFormat's ordinal (LumenCore/FrameFormat.h): 0 BGR24, 1 RGB24, 2 GRAY8,
// 3 NV12, 4 YUYV, 5 MJPEG, 6 Y10, 7 Y16, 8 Y10P, 9 Y10BPACK (raw mono sensor formats).
export interface CameraMode {
  Width: number;
  Height: number;
  Fps: number;
  PixelFormat: number;
  IsNative: boolean;
}

// mirrors CameraControlRangeDto/CameraControlsDto: a control's range on this camera (units differ
// per device). Supported=false means no such control.
export interface CameraControlRange {
  Supported: boolean;
  Minimum: number;
  Maximum: number;
  Step: number;
  Default: number;
  Value: number;
}

export interface CameraControls {
  Exposure: CameraControlRange;
  Gain: CameraControlRange;
}

// mirrors CalibrationStatusDto: whether a camera has a saved calibration and whether it matches
// the current capture mode
export interface CalibrationStatus {
  HasCalibration: boolean;
  MatchesCurrentResolution: boolean;
  CalibratedWidth: number | null;
  CalibratedHeight: number | null;
}

// mirrors CalibrationController's board query parameters; boardType is CalibrationBoardType (0 checkerboard, 1 ChArUco)
export interface CalibrationBoard {
  boardType: number;
  rows: number;
  cols: number;
  squareSizeMeters: number;
  markerSizeMeters?: number;
}

// mirrors CalibrationSessionDto; PreviewSinkId is an MJPEG sink streamed from /stream/mjpeg
export interface CalibrationSession {
  SessionId: number;
  Kind: 'camera' | 'stereo';
  PreviewSinkId: number;
}

// mirrors StoredCalibrationDto / StoredStereoCalibrationDto
export interface StoredCameraCalibration {
  CameraPath: string;
  Result: CameraCalibrationResult;
  CalibratedAtUnixMs: number;
}
export interface StoredStereoCalibration {
  LeftCameraPath: string;
  RightCameraPath: string;
  Result: StereoCalibrationResult;
  CalibratedAtUnixMs: number;
}

// mirrors LumenCore's RefineEdgesMode (IApriltagBackend.h): which refine_edges implementation the Vulkan backend runs
export const REFINE_EDGES_MODES = [
  { value: 0, label: 'Upstream (reference)' },
  { value: 1, label: 'Exact (bit-identical, default)' },
  { value: 2, label: 'Fast (single precision)' },
  { value: 3, label: 'Ultra-fast (refine only decodable quads)' },
] as const;

// An uploaded ONNX object detection model (YOLOv8/YOLOv11), as returned by /model/getAll
export interface Model {
  id: number;
  name: string;
  variant: number; // 0 = YOLOv8, 1 = YOLOv11
  inputSize: number;
  confThreshold: number;
  nmsThreshold: number;
  // backend, derived from the uploaded file's extension: 0 = RKNN (NPU), 1 = ONNX Runtime
  provider: number;
}

// Extra fields AddSinkModal collects; handleAddSink dispatches on `type` to choose which apply.
export interface AddSinkOptions {
  tagSize?: number;
  backend?: number; // 0 = CPU, 1 = Vulkan
  modelId?: number;
  newModel?: {
    name: string;
    variant: number;
    inputSize: number;
    confThreshold: number;
    nmsThreshold: number;
    modelFile: File;
    labelsFile?: File;
  };
}

// Shared connection defaults for the per-node "Publish to NetworkTables" toggle, configured in Settings.
export interface NT4Defaults {
  mode: 'team' | 'server';
  teamNumber?: number;
  serverAddress?: string;
  port?: number;
  rootTable: string;
}

export interface SystemStats {
  sources: number;
  sinks: number;
  activeStreams: number;
  uptime: string;
  serverStatus: 'online' | 'offline' | 'error';
  cpuUsage?: number;
  ramUsage?: number;
  diskUsage?: number;
}

export interface Settings {
  serverUrl: string;
  nt4: NT4Defaults;
}

export interface Toast {
  message: string;
  type: 'success' | 'error' | 'info';
}

export interface WebRTCStreamProps {
  sinkId: number;
  onStop: () => void;
  onError: (error: string) => void;
  className?: string;
  // hides the header bar and fills its container, for small preview tiles
  compact?: boolean;
}

export interface ModalProps {
  isOpen: boolean;
  onClose: () => void;
}

export interface AddSourceModalProps extends ModalProps {
  onAdd: (name: string, type: string, file?: File, fps?: number, hardwareInfo?: CameraHardwareInfo) => void;
}

export interface SettingsModalProps extends ModalProps {
  settings: Settings;
  onSave: (settings: Settings) => void;
}

export interface ToastProps {
  message: string;
  type: 'success' | 'error' | 'info';
  onClose: () => void;
}

// Device monitoring types, sourced from /ws/state's WsDeviceStats; ramUsage is in megabytes.
export interface DeviceStats {
  cpuUsage: number;
  ramUsage: number;
  diskUsage: number;
}

// --- Stereo depth ---

// mirrors CameraCalibrationResultDto/StereoCalibrationResultDto field-for-field (PascalCase)
export interface CameraCalibrationResult {
  Fx: number; Fy: number; Cx: number; Cy: number; Rms: number;
  DistCoeffs: number[]; ImageWidth: number; ImageHeight: number;
}

// R/T/E/F/R1/R2/P1/P2/Q are flat row-major arrays; only EpipolarRms, BaselineMeters,
// RectifiedFx/Cx/Cy and the two CameraCalibrationResults are read by the WebUI, the rest is passed
// through to StereoDepthSink's create call.
// Epipolar RMS pass/fail gate (pixels), shared by StereoPage and the calibration wizards.
export const EPIPOLAR_RMS_GATE = 0.5;

export interface StereoCalibrationResult {
  Left: CameraCalibrationResult;
  Right: CameraCalibrationResult;
  R: number[]; T: number[]; E: number[]; F: number[];
  R1: number[]; R2: number[]; P1: number[]; P2: number[]; Q: number[];
  StereoRms: number;
  EpipolarRms: number; // the pass/fail gate, < 0.5px
  BaselineMeters: number;
  RectifiedFx: number; RectifiedCx: number; RectifiedCy: number;
  ImageWidth: number; ImageHeight: number;
  RoiLeftX: number; RoiLeftY: number; RoiLeftW: number; RoiLeftH: number;
  RoiRightX: number; RoiRightY: number; RoiRightW: number; RoiRightH: number;
}

// matches StereoDepthBackendKind.h - a plain C++ enum, so the values below are its declaration
// order (0-indexed), exactly what SWIG/System.Text.Json serialise an enum as.
export const StereoDepthBackendKind = {
  CODEC_AUTO: 0,
  CODEC_LAVC: 1,
  CODEC_RKMPP_HWENC: 2,
  SGBM: 3,
} as const;
export type StereoDepthBackendKindValue = typeof StereoDepthBackendKind[keyof typeof StereoDepthBackendKind];

export const STEREO_BACKEND_LABELS: Record<number, string> = {
  0: 'Auto (codec-stereo)',
  1: 'codec-stereo: lavc_sw (software, any platform)',
  2: 'codec-stereo: rkmpp_hwenc (Orange Pi hardware)',
  3: 'SGBM (OpenCV, CPU - accuracy reference)',
};

// matches StereoFrameOutput.h
export const StereoFrameOutput = {
  DEPTH_COLORMAP: 0,
  RECTIFIED_LEFT: 1,
  DEPTH_OVERLAY: 2,
} as const;
export type StereoFrameOutputValue = typeof StereoFrameOutput[keyof typeof StereoFrameOutput];

export const STEREO_FRAME_OUTPUT_LABELS: Record<number, string> = {
  0: 'Depth colormap',
  1: 'Rectified left (bind a detector here for DepthFusionNode)',
  2: 'Depth overlay',
};

// mirrors CalibrationCoverageDto: each Snapshots entry is one snapshot's detected corners,
// flattened as [x0,y0,x1,y1,...]
export interface CalibrationCoverage {
  FrameWidth: number;
  FrameHeight: number;
  Snapshots: number[][];
}

// mirrors Server/Dtos.cs's StereoDepthStatsDto (PascalCase)
export interface StereoDepthStats {
  ValidFraction: number;
  MedianDepthMeters: number;
}

// --- pipeline profiles, the /ws/state channel and node capabilities ---
// These mirror the C# response shapes field-for-field in PascalCase (EmbedIO's serialiser does not
// camelCase).

// mirrors Server/PipelineProfile.cs
export const PipelineProfileKind = { ApriltagSink: 0, ObjectDetectionSink: 1 } as const;
export interface PipelineProfile {
  Index: number;
  Name: string;
  Kind: number;
  TagSize: number | null;
  Backend: number | null;
  FrameWidth: number;
  FrameHeight: number;
  FieldLayoutPath: string | null;
  DriverMode: boolean;
  ModelId: number | null;
}

// mirrors Server/NodeCapabilities.cs
export interface NodeTypeCapability {
  TypeName: string;
  Category: 'source' | 'sink';
  DisplayName: string;
  Icon: string;
  MaxSources: number;
  SourceRoles: string[] | null;
  IsDualRoleSink: boolean;
  HasDepthAttach: boolean;
  Implemented: boolean;
}
export interface NodeTypesResponse {
  Sources: NodeTypeCapability[];
  Sinks: NodeTypeCapability[];
}

// mirrors Server/Source.cs, as embedded in /ws/state. CameraHardwareInfo here is lowercase
// {name,path} (the raw native struct); do not change it to PascalCase like the DTO version below.
export interface WsSource {
  CameraHardwareInfo: { name: string; path: string } | null;
  Fps: number | null;
  FilePath: string | null;
  Profiles: PipelineProfile[];
  ActiveProfileIndex: number;
  ActiveDetectionSinkId: number | null;
  Type: number; // 0 Camera, 1 ImageFile, 2 VideoFile, 3 SinkOutput - see SourceType in Source.cs
  Id: number;
  Name: string;
}

// mirrors Server/Sink.cs
export interface WsSink {
  Type: number; // SinkType ordinal - see mapSinkType in hooks/useAppData.ts for the string labels
  Id: number;
  Name: string;
  Source: WsSource | null;
  Source2: WsSource | null; // stereo sinks only - the RIGHT camera (Source is LEFT)
  DepthSourceId: number | null; // DepthFusionSink only
}

// mirrors Server/WebSockets/StateChannel.cs's own DTOs
export interface WsSinkState {
  Sink: WsSink;
  IsRunning: boolean;
}
export interface WsNodeStats {
  Fps: number;
  LatencyUs: number;
}
export interface WsDeviceStats {
  CpuUsagePercent: number;
  RamUsageMb: number;
  DiskUsagePercent: number;
  TemperatureC: number;
}
// mirrors Server/Dtos.cs's NetworkTablesStatusDto; the match view reads Connected from it
export interface NetworkTablesStatus {
  Connected: boolean;
  Identity: string;
  RootTable: string;
  TeamNumber: number | null;
  ServerAddress: string | null;
}

export interface StateSnapshot {
  Sources: WsSource[];
  Sinks: WsSinkState[];
  Device: WsDeviceStats;
  NodeStats: Record<string, WsNodeStats>;
}

// mirrors Server/Controllers/sinks/RecordSinkController.cs's RecordSegmentDto.
export interface RecordSegment {
  FileName: string;
  SizeBytes: number;
  LastWriteTimeUtc: string;
}

// mirrors Server/SnapshotService.cs's SnapshotEntry; Path is relative to the snapshot root
export interface SnapshotEntry {
  Camera: string;
  Path: string;
  Kind: 'input' | 'output';
  SizeBytes: number;
  CreatedUtc: string;
}

// mirrors Server/Dtos.cs's CameraControlDto: Kind 0 integer, 1 boolean, 2 menu (MenuValues/MenuLabels), 3 button
export const CameraControlKind = { Integer: 0, Boolean: 1, Menu: 2, Button: 3 } as const;
export interface CameraControl {
  Id: number;
  Name: string;
  Kind: number;
  Minimum: number;
  Maximum: number;
  Step: number;
  Default: number;
  Value: number;
  ReadOnly: boolean;
  Inactive: boolean;
  MenuLabels: string[];
  MenuValues: number[];
}

// mirrors Server/Dtos.cs's FrameTransformDto: crop (camera pixels; 0 width/height = none), then a clockwise rotation, then mirrors
export interface FrameTransform {
  Rotation: number;
  FlipHorizontal: boolean;
  FlipVertical: boolean;
  CropX: number;
  CropY: number;
  CropWidth: number;
  CropHeight: number;
}
