using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Threading;

using Server.Web;

namespace Server
{
    public enum CalibrationSessionKind { Camera, Stereo }

    // One interactive calibration run: a private native calibrator plus an MJPEG preview sink, both
    // kept out of SinkManager (and therefore out of the graph, data.json and graph profiles).
    public sealed class CalibrationSession
    {
        public int Id { get; init; }
        public CalibrationSessionKind Kind { get; init; }
        public int PreviewSinkId { get; init; }
        public int SourceId { get; init; }
        public int? Source2Id { get; init; }
        // ROI sources the session created to split one camera into eyes; deleted with the session
        public int[] OwnedRoiSourceIds { get; init; } = Array.Empty<int>();
        public string? CameraPath { get; init; }
        public string? Camera2Path { get; init; }
        // camera sources the session switched on, with whether each was already running beforehand
        public IReadOnlyDictionary<int, bool> ManagedSources { get; init; } = new Dictionary<int, bool>();
        public DateTime LastUsedUtc { get; set; } = DateTime.UtcNow;
    }

    // Owns the lifetime of calibration sessions. A session id is the native calibrator's id, so it comes from
    // the same allocator as graph node ids and cannot collide with them.
    public class CalibrationSessionManager
    {
        public static CalibrationSessionManager Instance { get; } = new CalibrationSessionManager();

        private static readonly TimeSpan IdleTimeout = TimeSpan.FromMinutes(15);

        private readonly ConcurrentDictionary<int, CalibrationSession> sessions = new();
        private readonly Timer idleSweep;

        private CalibrationSessionManager()
        {
            idleSweep = new Timer(_ => StopIdle(), null, TimeSpan.FromMinutes(1), TimeSpan.FromMinutes(1));
            AppDomain.CurrentDomain.ProcessExit += (_, _) => StopAll();
        }

        public IReadOnlyCollection<CalibrationSession> GetAll() => sessions.Values.ToList();

        public CalibrationSession Get(int sessionId)
        {
            if (!sessions.TryGetValue(sessionId, out var session))
                throw ApiException.NotFound($"no calibration session with id {sessionId}");
            session.LastUsedUtc = DateTime.UtcNow;
            return session;
        }

        private CalibrationSession GetOfKind(int sessionId, CalibrationSessionKind kind)
        {
            var session = Get(sessionId);
            if (session.Kind != kind) throw ApiException.BadRequest($"session {sessionId} is a {session.Kind} session");
            return session;
        }

        private static string RequireCameraPath(int sourceId)
        {
            Source source = SourceManager.Instance.GetSourceById(sourceId)
                ?? throw ApiException.NotFound($"no source with id {sourceId}");
            return source.CameraHardwareInfo?.path
                ?? throw ApiException.BadRequest($"source {sourceId} is not a camera");
        }

        // Binds the calibrator's overlay output to a fresh MJPEG sink served by /stream/mjpeg.
        private static int CreatePreview(int calibratorId)
        {
            int previewId = ManagerWrapper.Instance.CreateMjpegSink(80);
            ManagerWrapper.Instance.BindSourceToSink(calibratorId, previewId);
            ManagerWrapper.Instance.StartSinkById(previewId);
            return previewId;
        }

        public CalibrationSession StartCamera(int sourceId, CalibrationBoardType boardType, int rows, int cols,
            float squareSizeMeters, float markerSizeMeters, int arucoDictionaryId)
        {
            string cameraPath = RequireCameraPath(sourceId);
            bool wasActive = SourceManager.Instance.IsSourceActive(sourceId);

            int id = ManagerWrapper.Instance.CreateCameraCalibrator(boardType, rows, cols, squareSizeMeters, markerSizeMeters, arucoDictionaryId);
            int previewId = -1;
            try
            {
                ManagerWrapper.Instance.BindSourceToSink(sourceId, id);
                SourceManager.Instance.EnableSourceById(sourceId);
                ManagerWrapper.Instance.StartSinkById(id);
                previewId = CreatePreview(id);
            }
            catch
            {
                if (previewId >= 0) ManagerWrapper.Instance.DeleteSink(previewId);
                ManagerWrapper.Instance.DeleteSink(id);
                throw;
            }

            var session = new CalibrationSession
            {
                Id = id, Kind = CalibrationSessionKind.Camera, PreviewSinkId = previewId,
                SourceId = sourceId, CameraPath = cameraPath,
                ManagedSources = new Dictionary<int, bool> { [sourceId] = wasActive },
            };
            sessions[id] = session;
            return session;
        }

        // Left and right are two real camera sources.
        public CalibrationSession StartStereo(int leftSourceId, int rightSourceId, CalibrationBoardType boardType,
            int rows, int cols, float squareSizeMeters)
        {
            string leftPath = RequireCameraPath(leftSourceId);
            string rightPath = RequireCameraPath(rightSourceId);
            return StartStereoCore(leftSourceId, rightSourceId, leftPath, rightPath, Array.Empty<int>(),
                new[] { leftSourceId, rightSourceId }, boardType, rows, cols, squareSizeMeters);
        }

        // One side-by-side stereo camera: its frame is split into left/right halves by two ROI sources owned by the session.
        public CalibrationSession StartStereoSplit(int cameraSourceId, CalibrationBoardType boardType,
            int rows, int cols, float squareSizeMeters)
        {
            string cameraPath = RequireCameraPath(cameraSourceId);
            CameraMode mode = ManagerWrapper.Instance.GetCameraCurrentMode(cameraSourceId);
            int leftWidth = mode.width / 2;
            int rightWidth = mode.width - leftWidth;

            int leftId = ManagerWrapper.Instance.CreateRoiSource(cameraSourceId, 0, 0, leftWidth, mode.height);
            int rightId = ManagerWrapper.Instance.CreateRoiSource(cameraSourceId, leftWidth, 0, rightWidth, mode.height);
            try
            {
                return StartStereoCore(leftId, rightId, cameraPath, cameraPath, new[] { leftId, rightId },
                    new[] { cameraSourceId }, boardType, rows, cols, squareSizeMeters);
            }
            catch
            {
                ManagerWrapper.Instance.DeleteSource(leftId);
                ManagerWrapper.Instance.DeleteSource(rightId);
                throw;
            }
        }

        private CalibrationSession StartStereoCore(int leftId, int rightId, string? leftPath, string? rightPath,
            int[] ownedRoiIds, int[] managedSourceIds, CalibrationBoardType boardType, int rows, int cols, float squareSizeMeters)
        {
            var wasActive = managedSourceIds.ToDictionary(id => id, id => SourceManager.Instance.IsSourceActive(id));

            int id = ManagerWrapper.Instance.CreateStereoCalibrator(boardType, rows, cols, squareSizeMeters);
            int previewId = -1;
            try
            {
                if (!ManagerWrapper.Instance.BindStereoSources(id, leftId, rightId))
                    throw ApiException.BadRequest($"could not bind sources {leftId} and {rightId}");
                foreach (int sourceId in managedSourceIds) SourceManager.Instance.EnableSourceById(sourceId);
                foreach (int roiId in ownedRoiIds) ManagerWrapper.Instance.StartSinkById(roiId);
                ManagerWrapper.Instance.StartSinkById(id);
                previewId = CreatePreview(id);
            }
            catch
            {
                if (previewId >= 0) ManagerWrapper.Instance.DeleteSink(previewId);
                ManagerWrapper.Instance.DeleteSink(id);
                throw;
            }

            var session = new CalibrationSession
            {
                Id = id, Kind = CalibrationSessionKind.Stereo, PreviewSinkId = previewId,
                SourceId = leftId, Source2Id = rightId, OwnedRoiSourceIds = ownedRoiIds,
                CameraPath = leftPath, Camera2Path = rightPath,
                ManagedSources = wasActive,
            };
            sessions[id] = session;
            return session;
        }

        // Tears down the native nodes and disables any source this session switched on that no graph sink uses.
        public void Stop(int sessionId)
        {
            if (!sessions.TryRemove(sessionId, out var session)) return;

            ManagerWrapper.Instance.DeleteSink(session.PreviewSinkId);
            ManagerWrapper.Instance.DeleteSink(session.Id);

            foreach (int roiId in session.OwnedRoiSourceIds) ManagerWrapper.Instance.DeleteSource(roiId);
            foreach (var (sourceId, wasActive) in session.ManagedSources) ReleaseSource(sourceId, wasActive);
        }

        private static void ReleaseSource(int sourceId, bool wasActive)
        {
            if (wasActive) return;
            bool usedByGraph = SinkManager.Instance.GetAllSinks()
                .Any(s => s.Source?.Id == sourceId || s.Source2?.Id == sourceId);
            if (!usedByGraph) SourceManager.Instance.DisableSourceById(sourceId);
        }

        private void StopIdle()
        {
            var cutoff = DateTime.UtcNow - IdleTimeout;
            foreach (var session in sessions.Values.Where(s => s.LastUsedUtc < cutoff).ToList())
            {
                try { Stop(session.Id); } catch (Exception) { /* best effort; the next sweep retries nothing, the session is gone */ }
            }
        }

        private void StopAll()
        {
            foreach (int id in sessions.Keys.ToList())
            {
                try { Stop(id); } catch (Exception) { /* process is exiting */ }
            }
        }

        // --- camera session operations ---

        public bool SaveCameraDetection(int sessionId) =>
            ManagerWrapper.Instance.SaveCameraCalibrationBoardDetection(GetOfKind(sessionId, CalibrationSessionKind.Camera).Id);

        public int GetCameraSnapshotCount(int sessionId) =>
            ManagerWrapper.Instance.GetCameraCalibrationSnapshotCount(GetOfKind(sessionId, CalibrationSessionKind.Camera).Id);

        public bool RemoveCameraSnapshot(int sessionId, int index) =>
            ManagerWrapper.Instance.RemoveCameraCalibrationSnapshot(GetOfKind(sessionId, CalibrationSessionKind.Camera).Id, index);

        public void ClearCameraSnapshots(int sessionId) =>
            ManagerWrapper.Instance.ClearCameraCalibrationSnapshots(GetOfKind(sessionId, CalibrationSessionKind.Camera).Id);

        // Runs cv::calibrateCamera over the saved snapshots and persists the result under the camera's device path.
        public CameraCalibrationResult RunCamera(int sessionId)
        {
            var session = GetOfKind(sessionId, CalibrationSessionKind.Camera);
            var result = ManagerWrapper.Instance.RunCameraCalibration(session.Id);
            CalibrationManager.Instance.SaveResult(session.CameraPath, result);
            return result;
        }

        public CameraCalibrationResult GetCameraResult(int sessionId) =>
            ManagerWrapper.Instance.GetCameraCalibrationResult(GetOfKind(sessionId, CalibrationSessionKind.Camera).Id);

        public CalibrationCoverageDto GetCameraCoverage(int sessionId)
        {
            int id = GetOfKind(sessionId, CalibrationSessionKind.Camera).Id;
            int count = ManagerWrapper.Instance.GetCameraCalibrationSnapshotCount(id);
            var snapshots = new double[count][];
            for (int i = 0; i < count; i++)
                snapshots[i] = ManagerWrapper.Instance.GetCameraCalibrationSnapshotCorners(id, i).ToArray();
            return new CalibrationCoverageDto(
                ManagerWrapper.Instance.GetCameraCalibrationFrameWidth(id),
                ManagerWrapper.Instance.GetCameraCalibrationFrameHeight(id),
                snapshots);
        }

        // --- stereo session operations ---

        public bool SaveStereoDetection(int sessionId) =>
            ManagerWrapper.Instance.SaveStereoCalibrationDetection(GetOfKind(sessionId, CalibrationSessionKind.Stereo).Id);

        public int GetStereoPairCount(int sessionId) =>
            ManagerWrapper.Instance.GetStereoCalibrationPairCount(GetOfKind(sessionId, CalibrationSessionKind.Stereo).Id);

        public bool RemoveStereoPair(int sessionId, int index) =>
            ManagerWrapper.Instance.RemoveStereoCalibrationPair(GetOfKind(sessionId, CalibrationSessionKind.Stereo).Id, index);

        public void ClearStereoPairs(int sessionId) =>
            ManagerWrapper.Instance.ClearStereoCalibrationPairs(GetOfKind(sessionId, CalibrationSessionKind.Stereo).Id);

        // Runs cv::stereoCalibrate + cv::stereoRectify over the saved pairs and persists the result under both device paths.
        public StereoCalibrationResult RunStereo(int sessionId)
        {
            var session = GetOfKind(sessionId, CalibrationSessionKind.Stereo);
            var result = ManagerWrapper.Instance.RunStereoCalibration(session.Id);
            StereoCalibrationManager.Instance.SaveResult(session.CameraPath, session.Camera2Path, result);
            return result;
        }

        public StereoCalibrationResult GetStereoResult(int sessionId) =>
            ManagerWrapper.Instance.GetStereoCalibrationResult(GetOfKind(sessionId, CalibrationSessionKind.Stereo).Id);

        public CalibrationCoverageDto GetStereoCoverage(int sessionId, string eye)
        {
            int id = GetOfKind(sessionId, CalibrationSessionKind.Stereo).Id;
            int count = ManagerWrapper.Instance.GetStereoCalibrationPairCount(id);
            var pairs = new double[count][];
            for (int i = 0; i < count; i++)
                pairs[i] = ManagerWrapper.Instance.GetStereoCalibrationPairCorners(id, i, eye).ToArray();
            return new CalibrationCoverageDto(
                ManagerWrapper.Instance.GetStereoCalibrationFrameWidth(id),
                ManagerWrapper.Instance.GetStereoCalibrationFrameHeight(id),
                pairs);
        }

        // The stereo calibrator's per-frame JSON status (foundLeft, foundRight, skew).
        public string GetStatusJson(int sessionId) =>
            ManagerWrapper.Instance.GetSinkResult(Get(sessionId).Id);
    }
}
