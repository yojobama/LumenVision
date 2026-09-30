using System;
using System.IO;
using System.Linq;

namespace Server
{
    public enum SnapshotKind { Input, Output }

    // Saves still images of a camera: the raw frame (input) or the active detector's annotated frame (output) under
    // snapshots/<camera name>/<UTC timestamp>-<kind>.jpg.
    public sealed class SnapshotService
    {
        public static SnapshotService Instance { get; } = new SnapshotService();

        public static string SnapshotRoot => Path.Combine(AppContext.BaseDirectory, "snapshots");

        // The path a snapshot of this camera would be written to now, relative to SnapshotRoot with forward slashes.
        public static string RelativePath(string cameraName, SnapshotKind kind, DateTime utcNow)
        {
            string folder = SanitiseName(cameraName);
            return $"{folder}/{utcNow:yyyyMMdd-HHmmss-fff}-{kind.ToString().ToLowerInvariant()}.jpg";
        }

        // file and folder names made of letters, digits, '.', '_' and '-' only
        public static string SanitiseName(string name)
        {
            string cleaned = new string((name ?? string.Empty).Select(c => char.IsLetterOrDigit(c) || c == '.' || c == '_' || c == '-' ? c : '_').ToArray()).Trim('.');
            return cleaned.Length == 0 ? "camera" : cleaned;
        }

        // Returns the relative path of the saved file, or null if no frame was available. An output snapshot of a camera with no
        // detection running falls back to its input frame.
        public string? Save(int sourceId, SnapshotKind kind)
        {
            Source source = SourceManager.Instance.GetSourceById(sourceId)
                ?? throw new ArgumentException($"no source with id {sourceId}");

            int nodeId = sourceId;
            if (kind == SnapshotKind.Output && source.ActiveDetectionSinkId.HasValue)
                nodeId = source.ActiveDetectionSinkId.Value;

            string relative = RelativePath(source.Name, kind, DateTime.UtcNow);
            string full = Path.Combine(SnapshotRoot, relative.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(full)!);
            return ManagerWrapper.Instance.SaveFreshSnapshot(nodeId, full) ? relative : null;
        }
    }
}
