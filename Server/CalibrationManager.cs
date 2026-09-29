using System.Text.Json;

namespace Server
{
    public class StoredCalibration
    {
        public string CameraPath { get; set; } = "";
        public CameraCalibrationResult Result { get; set; } = new CameraCalibrationResult();
        public long CalibratedAtUnixMs { get; set; }
    }

    // Reports whether a camera has a calibration and whether it matches the camera's current resolution.
    public record struct CalibrationStatus(bool HasCalibration, bool MatchesCurrentResolution, int? CalibratedWidth, int? CalibratedHeight);

    // Persists calibration results keyed by camera device path + resolution (a result is only valid
    // at the resolution it was computed at), so they survive a server restart.
    public class CalibrationManager
    {
        public static CalibrationManager Instance { get; } = new CalibrationManager();

        private readonly string path = "calibrations.json";
        private readonly List<StoredCalibration> calibrations = new();

        private CalibrationManager()
        {
            Load();
        }

        private void Load()
        {
            if (!File.Exists(path)) return;
            try
            {
                var loaded = JsonSerializer.Deserialize<List<StoredCalibration>>(File.ReadAllText(path));
                if (loaded != null) calibrations.AddRange(loaded);
            }
            catch (Exception) { /* corrupt file - start empty rather than crash the server */ }
        }

        private void Save()
        {
            File.WriteAllText(path, JsonSerializer.Serialize(calibrations, new JsonSerializerOptions { WriteIndented = true }));
        }

        // resolves the camera path the calibrator sink is bound to and persists the result under it
        public void SaveResult(int calibratorSinkId, CameraCalibrationResult result) =>
            SaveResult(SinkManager.Instance.GetSinkById(calibratorSinkId)?.Source?.CameraHardwareInfo?.path, result);

        // persists the result under the camera's device path; not persisted without one (e.g. a video file)
        public void SaveResult(string? cameraPath, CameraCalibrationResult result)
        {
            if (cameraPath == null) return;

            calibrations.RemoveAll(c => c.CameraPath == cameraPath && c.Result.imageWidth == result.imageWidth && c.Result.imageHeight == result.imageHeight);
            calibrations.Add(new StoredCalibration
            {
                CameraPath = cameraPath,
                Result = result,
                CalibratedAtUnixMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
            });
            Save();
        }

        public StoredCalibration? GetLatest(string cameraPath, int width, int height)
        {
            return calibrations
                .Where(c => c.CameraPath == cameraPath && c.Result.imageWidth == width && c.Result.imageHeight == height)
                .OrderByDescending(c => c.CalibratedAtUnixMs)
                .FirstOrDefault();
        }

        // The saved calibration for a camera source at its current resolution; null for non-camera sources or when none matches.
        public CameraCalibrationResult? GetForSource(int sourceId)
        {
            string? cameraPath = SourceManager.Instance.GetSourceById(sourceId)?.CameraHardwareInfo?.path;
            if (cameraPath == null) return null;
            CameraMode mode = ManagerWrapper.Instance.GetCameraCurrentMode(sourceId);
            return GetLatest(cameraPath, mode.width, mode.height)?.Result;
        }

        public List<StoredCalibration> GetAll() => calibrations;

        // Ignores the current resolution when looking up, then compares against the camera's actual mode,
        // to tell "never calibrated" from "calibrated at a different resolution".
        public CalibrationStatus GetCalibrationStatus(int sourceId)
        {
            Source? source = SourceManager.Instance.GetSourceById(sourceId);
            string? cameraPath = source?.CameraHardwareInfo?.path;
            if (cameraPath == null) return new CalibrationStatus(false, false, null, null);

            StoredCalibration? latest = calibrations
                .Where(c => c.CameraPath == cameraPath)
                .OrderByDescending(c => c.CalibratedAtUnixMs)
                .FirstOrDefault();
            if (latest == null) return new CalibrationStatus(false, false, null, null);

            CameraMode currentMode = ManagerWrapper.Instance.GetCameraCurrentMode(sourceId);
            bool matches = latest.Result.imageWidth == currentMode.width && latest.Result.imageHeight == currentMode.height;
            return new CalibrationStatus(true, matches, latest.Result.imageWidth, latest.Result.imageHeight);
        }
    }
}
