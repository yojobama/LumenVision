using System.Text.Json;

namespace Server
{
    public class StoredStereoCalibration
    {
        public string LeftCameraPath { get; set; } = "";
        public string RightCameraPath { get; set; } = "";
        public StereoCalibrationResult Result { get; set; } = new StereoCalibrationResult();
        public long CalibratedAtUnixMs { get; set; }
    }

    // Persists stereo calibration results keyed by both cameras' device paths + resolution
    // (the camera-pair equivalent of CalibrationManager).
    public class StereoCalibrationManager
    {
        public static StereoCalibrationManager Instance { get; } = new StereoCalibrationManager();

        private readonly string path = "stereoCalibrations.json";
        private readonly List<StoredStereoCalibration> calibrations = new();

        private StereoCalibrationManager()
        {
            Load();
        }

        private void Load()
        {
            if (!File.Exists(path)) return;
            try
            {
                var loaded = JsonSerializer.Deserialize<List<StoredStereoCalibration>>(File.ReadAllText(path));
                if (loaded != null) calibrations.AddRange(loaded);
            }
            catch (Exception) { /* corrupt file - start empty rather than crash the server */ }
        }

        private void Save()
        {
            File.WriteAllText(path, JsonSerializer.Serialize(calibrations, new JsonSerializerOptions { WriteIndented = true }));
        }

        // resolves the two camera paths the StereoCalibrationSink is bound to (Source = left, Source2 = right)
        // and persists the result under them; not persisted unless bound to two real camera sources.
        public void SaveResult(int calibratorSinkId, StereoCalibrationResult result)
        {
            var sink = SinkManager.Instance.GetSinkById(calibratorSinkId);
            string? leftPath = sink?.Source?.CameraHardwareInfo?.path;
            string? rightPath = sink?.Source2?.CameraHardwareInfo?.path;
            if (leftPath == null || rightPath == null) return;

            calibrations.RemoveAll(c => c.LeftCameraPath == leftPath && c.RightCameraPath == rightPath &&
                c.Result.imageWidth == result.imageWidth && c.Result.imageHeight == result.imageHeight);
            calibrations.Add(new StoredStereoCalibration
            {
                LeftCameraPath = leftPath,
                RightCameraPath = rightPath,
                Result = result,
                CalibratedAtUnixMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
            });
            Save();
        }

        public StoredStereoCalibration? GetLatest(string leftCameraPath, string rightCameraPath, int width, int height)
        {
            return calibrations
                .Where(c => c.LeftCameraPath == leftCameraPath && c.RightCameraPath == rightCameraPath &&
                    c.Result.imageWidth == width && c.Result.imageHeight == height)
                .OrderByDescending(c => c.CalibratedAtUnixMs)
                .FirstOrDefault();
        }

        public List<StoredStereoCalibration> GetAll() => calibrations;
    }
}
