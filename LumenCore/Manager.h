#pragma once
#include <vector>
#include <string>
#include <map>
#include <random>
#include <chrono>
#include <memory>

#include "ISink.h"
#include "CameraCalibrationResult.h"
#include "Logger.h"
#include "ISource.h"
#include "IDetectionBackend.h"
#include "IApriltagBackend.h"
#include "CalibrationBoardType.h"
#include "StereoCalibrationResult.h"
#include "StereoDepthBackendKind.h"
#include "StereoFrameOutput.h"
#include "CameraMode.h"

using namespace std;

typedef struct {
	string name;
	string path;
} CameraHardwareInfo;

enum ObjectDetectionProvider
{
	RKNN,
	ONNX
};

//enum Platform {
//	ORANGE_PI,
//	JETSON,
//	NVDIA_DEV_X86_64,
//	CPU_DEV_X86_64
//};

//class CameraCalibrationSink;
class SystemMonitor;

class Manager
{
public:
	Manager(string logFile);
	Manager();
	~Manager();

	// utility functions
	vector<int> GetAllSinks();
	vector<int> GetAllSources();

	std::vector<std::string> GetAvailableVideoEncoders();

	vector<CameraHardwareInfo> EnumerateAvailableCameras();
	bool BindSourceToSink(int sourceId, int sinkId);
	bool UnbindSourceFromSink(int sinkId);

	// functions to create frame sources
	int CreateCameraSource(CameraHardwareInfo info);
	int CreateCameraSource(CameraHardwareInfo info, int id);

	// Explicit resolution/fps/exposure control; throws if sourceId isn't a camera source (a caller bug), unlike an unsupported control (returns false).
	vector<CameraMode> GetCameraModes(int sourceId);
	CameraMode GetCameraCurrentMode(int sourceId);
	bool SetCameraMode(int sourceId, CameraMode mode);
	bool SetCameraExposure(int sourceId, int exposureAbsolute);
	bool SetCameraAutoExposure(int sourceId, bool enabled);
	bool SetCameraGain(int sourceId, int gain);
	// the device's range/current value for the controls the setters above drive (supported=false if the camera has no such control)
	CameraControlRange GetCameraExposureRange(int sourceId);
	CameraControlRange GetCameraGainRange(int sourceId);

	// splits one upstream source's frames into a fixed rectangular crop, zero-copy: create two against the same camera (one per eye)
	// and bind each into StereoCalibrator/StereoDepthNode. Binds itself to upstreamSourceId; throws if it doesn't exist.
	int CreateRoiSource(int upstreamSourceId, int x, int y, int width, int height);

	// Gives an ObjectDetectionSink the camera calibration it needs to report each detection's yaw/pitch; a no-op for other nodes
	void SetObjectDetectionCalibration(int sinkId, CameraCalibrationResult calibrationResult);

	// Driver mode: throws if sinkId isn't a detection sink that supports it (ApriltagDetector/ObjectDetectionSink).
	void SetDriverMode(int sinkId, bool enabled);
	bool GetDriverMode(int sinkId);

	// loads a WPILib-format AprilTagFieldLayout JSON file onto an ApriltagDetector sink (throws if sinkId isn't one), enabling
	// a jointly-solved field-relative camera pose from frames with 2+ visible field-pose tags.
	bool LoadFieldLayout(int sinkId, string jsonPath);
	int GetFieldLayoutTagCount(int sinkId);

	// saves sourceId's most recently published frame to a file (format from the extension, via cv::imwrite). Returns false if no frame
	// was published yet or the write fails; throws only if sourceId doesn't exist.
	bool SaveSnapshot(int sourceId, string path);
	// Like SaveSnapshot, but asks the node for a new frame first (colour, and annotated for a detector) and waits up to a second for it,
	// so it works with nothing streaming that node. Falls back to its latest frame if none arrives (e.g. the source is stopped).
	bool SaveFreshSnapshot(int sourceId, string path);
	// Caps how many results per second the node publishes (<= 0: unlimited); see ISource::SetFpsLimit
	void SetSourceFpsLimit(int sourceId, int fps);
	int GetSourceFpsLimit(int sourceId);
	int CreateVideoFileSource(string path, int fps);
	int CreateVideoFileSource(string path, int fps, int id);
	int CreateImageFileSource(string path);
	int CreateImageFileSource(string path, int id);

	// functions to create detection sinks
	int CreateApriltagDetector(CameraCalibrationResult calibrationResult, double tagSize /* metres */);
	int CreateApriltagDetector(int id, CameraCalibrationResult calibrationResult, double tagSize /* metres */);
	// creates an ApriltagDetector with an empty calibration result; use BindSourceToSink + CreateApriltagDetectorFromCalibrator
	// (or GetCameraCalibrationResult) to supply real calibration data once available
	int CreateApriltagDetector();
	int CreateApriltagDetector(int id);

	// same as above, with an explicit backend. frameWidth/frameHeight are only used for APRILTAG_BACKEND_VULKAN (its GPU buffers are sized
	// at construction) and may be 0: the detector then sizes itself from its first frame. Vulkan falls back to CPU if unavailable (see
	// GetApriltagDetectorBackendName). nthreads/quadDecimate <=0 use the backend default; see ApriltagTuning (IApriltagBackend.h).
	int CreateApriltagDetector(CameraCalibrationResult calibrationResult, double tagSize,
		ApriltagBackendKind backendKind, int frameWidth, int frameHeight,
		int nthreads = 0, float quadDecimate = 0.0f, bool refineEdges = true);
	int CreateApriltagDetector(int id, CameraCalibrationResult calibrationResult, double tagSize,
		ApriltagBackendKind backendKind, int frameWidth, int frameHeight,
		int nthreads = 0, float quadDecimate = 0.0f, bool refineEdges = true);
	// same, taking every tuning knob as one struct (ApriltagTuning, IApriltagBackend.h); new knobs are added there
	int CreateApriltagDetector(CameraCalibrationResult calibrationResult, double tagSize,
		ApriltagBackendKind backendKind, int frameWidth, int frameHeight, ApriltagTuning tuning);
	int CreateApriltagDetector(int id, CameraCalibrationResult calibrationResult, double tagSize,
		ApriltagBackendKind backendKind, int frameWidth, int frameHeight, ApriltagTuning tuning);
	string GetApriltagDetectorBackendName(int sinkId);
	// the requested/resolved backend (as an enum, not the display name) plus everything else needed to rebuild an equivalent detector
	// (tag size, calibration, tuning).
	ApriltagBackendKind GetApriltagDetectorBackendKind(int sinkId);
	double GetApriltagDetectorTagSize(int sinkId);
	CameraCalibrationResult GetApriltagDetectorCalibration(int sinkId);
	int GetApriltagDetectorThreads(int sinkId);
	float GetApriltagDetectorQuadDecimate(int sinkId);
	bool GetApriltagDetectorQuadDecimateSupported(int sinkId);
	bool GetApriltagDetectorRefineEdges(int sinkId);
	RefineEdgesMode GetApriltagDetectorRefineMode(int sinkId);
	// false on the CPU backend, which always runs REFINE_UPSTREAM
	bool GetApriltagDetectorRefineModeSupported(int sinkId);
	// legacy no-model overloads: inference needs a model, so these only keep generated SWIG call sites compiling and throw a clear error
	int CreateObjectDetectionSink(ObjectDetectionProvider provider);
	int CreateObjectDetectionSink(ObjectDetectionProvider provider, int id);

	// RKNN runs on the RK3588 NPU and is only compiled in when LUMEN_WITH_RKNN is on; selecting it otherwise throws rather than falling back to ONNX.
	int CreateObjectDetectionSink(ObjectDetectionProvider provider, string modelPath, string labelsPath,
		YoloVariant variant, float confThreshold, float nmsThreshold, int inputSize);
	int CreateObjectDetectionSink(int id, ObjectDetectionProvider provider, string modelPath, string labelsPath,
		YoloVariant variant, float confThreshold, float nmsThreshold, int inputSize);

	// which backend an existing ObjectDetectionSink is running (read-only; see ObjectDetectionSink::GetBackendName).
	std::string GetObjectDetectionSinkBackendName(int sinkId);

	// functions to create/manage camera calibrators. Default board is a 6x9 checkerboard with 25mm squares; the explicit-config overloads take
	// primitives plus the plain CalibrationBoardType enum, as CameraCalibrator.h's charuco header must not reach swig.i.
	int CreateCameraCalibrator();
	int CreateCameraCalibrator(int id);
	int CreateCameraCalibrator(CalibrationBoardType boardType, int rows, int cols,
		float squareSizeMeters, float markerSizeMeters, int arucoDictionaryId);
	int CreateCameraCalibrator(int id, CalibrationBoardType boardType, int rows, int cols,
		float squareSizeMeters, float markerSizeMeters, int arucoDictionaryId);

	// retrieves the calibration result of a CameraCalibrator sink, identified via dynamic_cast
	CameraCalibrationResult GetCameraCalibrationResult(int calibratorId);
	// explicitly runs cv::calibrateCamera over every snapshot saved so far and caches the
	// result (also returned by GetCameraCalibrationResult afterwards); throws if fewer than 4
	// snapshots have been saved
	CameraCalibrationResult RunCameraCalibration(int calibratorId);

	int GetCameraCalibrationSnapshotCount(int calibratorId);
	bool RemoveCameraCalibrationSnapshot(int calibratorId, int index);
	void ClearCameraCalibrationSnapshots(int calibratorId);

	// coverage-heatmap data for the calibration wizard: one saved snapshot's corner points, flattened [x0,y0,x1,y1,...]; empty if calibratorId
	// doesn't exist or index is out of range. GetCameraCalibrationFrameWidth/Height are 0 until a snapshot has been saved.
	vector<double> GetCameraCalibrationSnapshotCorners(int calibratorId, int index);
	int GetCameraCalibrationFrameWidth(int calibratorId);
	int GetCameraCalibrationFrameHeight(int calibratorId);

	// saves the checkerboard corners detected in the CameraCalibrator's latest frame as a calibration
	// snapshot to be used in the calibration phase. returns false if no board was detected yet.
	bool SaveCameraCalibrationBoardDetection(int calibratorId);

	// creates an ApriltagDetector using the calibration result produced by an existing CameraCalibrator,
	// transferring the calibration data so the detector can compute the real-world tag location
	int CreateApriltagDetectorFromCalibrator(int calibratorId, double tagSize /* metres */);
	int CreateApriltagDetectorFromCalibrator(int id, int calibratorId, double tagSize /* metres */);

	// --- Stereo depth ---
	//
	// StereoCalibrator takes primitives only, as StereoCalibrator.h's <opencv2/calib3d.hpp> must not reach swig.i. ChArUco is not supported,
	// so there is no marker size / dictionary parameter.
	int CreateStereoCalibrator();
	int CreateStereoCalibrator(int id);
	int CreateStereoCalibrator(CalibrationBoardType boardType, int rows, int cols, float squareSizeMeters);
	int CreateStereoCalibrator(int id, CalibrationBoardType boardType, int rows, int cols, float squareSizeMeters);

	// binds two sources as the explicit left/right roles of a stereo sink (StereoCalibrator or StereoDepthNode): binds via ISink::BindSource
	// and records the roles by source ID, since bind order alone can swap them (see IStereoRoleReceiver.h).
	bool BindStereoSources(int sinkId, int leftSourceId, int rightSourceId);

	bool SaveStereoCalibrationDetection(int calibratorId);
	int GetStereoCalibrationPairCount(int calibratorId);
	bool RemoveStereoCalibrationPair(int calibratorId, int index);
	void ClearStereoCalibrationPairs(int calibratorId);

	// same reasoning as GetCameraCalibrationSnapshotCorners - eye must be "left" or "right".
	vector<double> GetStereoCalibrationPairCorners(int calibratorId, int index, string eye);
	int GetStereoCalibrationFrameWidth(int calibratorId);
	int GetStereoCalibrationFrameHeight(int calibratorId);
	// runs cv::stereoCalibrate + cv::stereoRectify over every saved pair; throws if fewer than 8
	// pairs have been saved. Also returned by GetStereoCalibrationResult afterwards.
	StereoCalibrationResult RunStereoCalibration(int calibratorId);
	StereoCalibrationResult GetStereoCalibrationResult(int calibratorId);

	// StereoDepthNode: bind with BindStereoSources. `calibration` is normally GetStereoCalibrationResult from a StereoCalibrator sink.
	int CreateStereoDepthNode(StereoDepthBackendKind backend, StereoCalibrationResult calibration,
		double minDepthMeters, double maxDepthMeters, int maxSkewUs, StereoFrameOutput frameOutput);
	int CreateStereoDepthNode(int id, StereoDepthBackendKind backend, StereoCalibrationResult calibration,
		double minDepthMeters, double maxDepthMeters, int maxSkewUs, StereoFrameOutput frameOutput);
	// which backend actually ended up running (e.g. "lavc_sw", "rkmpp_hwenc", "sgbm")
	string GetStereoDepthBackendName(int sinkId);
	// fraction of blocks that came back valid in the most recent pair and their median depth in metres (the full per-block grid is in GetSinkResult's JSON).
	double GetStereoDepthValidFraction(int sinkId);
	double GetStereoDepthMedianDepthMeters(int sinkId);

	// DepthFusionNode: fuses a detector's bounding boxes with a StereoDepthNode's depth grid. Bind the detector with BindSourceToSink (it must be
	// bound to the StereoDepthNode's rectified-left output; see DepthFusionNode.h); attach the depth source separately, as it is read in-process
	// rather than via SourceResult/JSON (see StereoDepthNode::GetLastDepthGrid).
	int CreateDepthFusionNode();
	int CreateDepthFusionNode(int id);
	bool SetDepthFusionDepthNode(int fusionSinkId, int depthNodeSourceId);

	// terminal sink: bind any JSON-producing source to it and it publishes onto the configured NT4 server. Takes only primitive parameters, as
	// NetworkTablesSink.h's ntcore C++ API must never reach swig.i.
	//
	// Declared unconditionally (not #ifdef LUMEN_WITH_NT4) so every configuration generates the same C# API from swig.i (see
	// cmake/LumenFeatures.cmake); with NT4 compiled out, calling these throws a "not compiled into this build" error. See GetEnabledFeatures().
	int CreateNetworkTablesSinkForTeam(int teamNumber, string rootTable, string clientIdentity);
	int CreateNetworkTablesSinkForTeam(int id, int teamNumber, string rootTable, string clientIdentity);
	int CreateNetworkTablesSinkForServer(string serverAddress, int port, string rootTable, string clientIdentity);
	int CreateNetworkTablesSinkForServer(int id, string serverAddress, int port, string rootTable, string clientIdentity);
	bool IsNetworkTablesSinkConnected(int sinkId);
	string GetNetworkTablesSinkStatus(int sinkId);
	// drains every pending robot-writable config/pipelineIndex and config/driverMode write seen since the last call, as a JSON array (see
	// NetworkTablesSink::PollConfigRequests). The caller applies each request, e.g. driverMode via SetDriverMode().
	string PollNetworkTablesSinkConfigRequests(int sinkId);
	// the coprocessor-wide robot-writable "<root>/config/recording" request: -1 = no new write
	// since the last call, 0 = stop, 1 = start (see NetworkTablesSink::PollRecordingRequest)
	int PollNetworkTablesSinkRecordingRequest(int sinkId);
	// publishes "<root>/status/recording" on that sink's NT connection
	void SetNetworkTablesSinkRecordingStatus(int sinkId, bool recording);
	// the coprocessor-wide robot-writable "<root>/config/ledMode": -2 = no new write since the last call, else -1 default / 0 off / 1 on / 2 blink
	int PollNetworkTablesSinkLedRequest(int sinkId);
	void SetNetworkTablesSinkLedStatus(int sinkId, int mode);
	// publishes "<root>/<node>/status/{pipelineIndex,driverMode,fpsLimit}" (see NetworkTablesSink::PublishNodeStatus)
	void SetNetworkTablesSinkNodeStatus(int sinkId, string nodeId, int pipelineIndex, bool driverMode, int fpsLimit);
	// publishes a node's topics under `alias` instead of its id (see NetworkTablesSink::SetNodeAlias); "" clears it
	void SetNetworkTablesSinkNodeAlias(int sinkId, string nodeId, string alias);

	// terminal sink: bind any single frame-producing node and it encodes and streams it over WebRTC. Takes only primitive parameters, as
	// WebRTCSink.h's libdatachannel API must never reach swig.i. Declared unconditionally like the NT4 methods above.
	int CreateWebRTCSink(int bitrateKbps, int fps, string encoderName);
	int CreateWebRTCSink(int id, int bitrateKbps, int fps, string encoderName);
	// "h264_rkmpp" if this build's ffmpeg has it (RK3588 hardware encode via nyanmisaka/ffmpeg-rockchip), else "libx264"; a runtime probe
	// (avcodec_find_encoder_by_name).
	string GetPreferredWebRTCEncoder();
	// non-trickle ICE: blocks until this peer's candidate gathering completes (bounded by a
	// timeout inside WebRTCSink), then returns one complete SDP offer
	string WebRTCCreateOffer(int sinkId);
	void WebRTCSetAnswer(int sinkId, string sdp);
	void WebRTCAddIceCandidate(int sinkId, string candidate, string mid);
	bool IsWebRTCSinkConnected(int sinkId);
	string GetWebRTCSinkStatus(int sinkId);

	// terminal sink: a simple JPEG preview stream (see MjpegSink.h). Always available (pure OpenCV); declared here to keep this header's
	// SWIG-visible surface small.
	int CreateMjpegSink(int jpegQuality);
	int CreateMjpegSink(int id, int jpegQuality);
	// base64-encoded JPEG (see MjpegSink::GetLatestJpegBase64). Empty string if sinkId isn't an MjpegSink or no frame has arrived yet.
	string GetMjpegFrameBase64(int sinkId);

	// terminal sink: bind any single frame-producing node and record it to segmented MP4 files plus a JSON-Lines telemetry sidecar (see
	// RecordSink.h). Takes only primitive parameters, as RecordSink.h's libavformat API must never reach swig.i. Declared unconditionally.
	int CreateRecordSink(string dstFolder, string encoderName, int bitrateKbps, int fps, int segmentSeconds, int64_t maxFolderSizeBytes, int maxFileCount);
	int CreateRecordSink(int id, string dstFolder, string encoderName, int bitrateKbps, int fps, int segmentSeconds, int64_t maxFolderSizeBytes, int maxFileCount);
	// filenames only, newest first (see RecordSink::ListSegments). Empty if sinkId isn't a RecordSink.
	vector<string> GetRecordSinkSegments(int sinkId);
	// true if filename existed under this sink's own dstFolder and was removed.
	bool DeleteRecordSinkSegment(int sinkId, string filename);

	// which LUMEN_WITH_* backends this build has compiled in (e.g. {"ONNX", "NT4", "VULKAN_APRILTAG"}), so the WebUI can grey out unavailable options.
	vector<string> GetEnabledFeatures();

	// stops and removes a node; also unbinds it from any sink that referenced it as a source
	bool DeleteSink(int sinkId);
	bool DeleteSource(int sourceId);

	void StartAllSources();
	void StopAllSources();
	bool StopSourceById(int sourceId);
	bool StartSourceById(int sourceId);
	bool IsSourceActive(int sourceId);

	void StartAllSinks();
	void StopAllSinks();
	bool StartSinkById(int sinkId);
	bool StopSinkById(int sinkId);
	bool IsSinkActive(int sinkId);

	string GetSinkResult(int sinkId);
	string GetAllSinkResults();

	//vector<string> GetRecording(int recorderId); // TODO: implement a recording mechanisem

	//int CreateCameraCalibrationSink(int width, int height);
	//void BindSourceToCalibrationSink(int sourceId);
	//void CameraCalibrationSinkGrabFrame(int sinkId);

	//CameraCalibrationResult GetCameraCalibrationResults(int sinkId);
	
	// functions to check system status
	int GetMemoryUsageBytes();
	int GetCPUUsage();
	int GetCpuTemperature();
	int GetDiskUsage();

	// live per-node throughput counter for any id in m_Sources (a camera/file source, or a dual-role sink acting as its own source; see
	// Manager::DeleteSink). 0 for anything else (terminal sinks produce no frames).
	//
	// FPS is not computed here; the C# WS broadcast loop takes the delta of this counter once per tick.
	uint64_t GetFrameCount(int id);
	// producedTimeUs - captureTimeUs of the most recent frame; 0 if id doesn't exist in
	// m_Sources or no frame has been produced yet.
	int64_t GetLatencyUs(int id);
private:
	//bool SetSinkResult(int sinkId, string result);

	int GenerateUUID();

	// maps for storing results, sources and sinks
	map<int, std::shared_ptr<ISource>> m_Sources;
	map<int, std::shared_ptr<ISink>> m_Sinks;
	//map<int, std::shared_ptr<CameraCalibrationSink>> m_CameraCalibrationSinks; // camera calibration sinks

	std::shared_ptr<Logger> m_Logger; // a logger for the entire application

	SystemMonitor* m_SystemMonitor; // system monitor for CPU, memory and disk usage
};

