using System;
using System.IO;
using System.Linq;

namespace Server
{
    public enum SnapshotKind { Input, Output }

    // One saved snapshot as the API lists it; Path is relative to the snapshot root with forward slashes.
    public sealed record SnapshotEntry(string Camera, string Path, string Kind, long SizeBytes, DateTime CreatedUtc);

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

        // Every saved snapshot, newest first; empty when nothing has been saved yet.
        public SnapshotEntry[] List()
        {
            if (!Directory.Exists(SnapshotRoot)) return Array.Empty<SnapshotEntry>();
            return Directory.EnumerateFiles(SnapshotRoot, "*.jpg", SearchOption.AllDirectories)
                .Select(file => new FileInfo(file))
                .Select(info =>
                {
                    string relative = System.IO.Path.GetRelativePath(SnapshotRoot, info.FullName).Replace('\\', '/');
                    string name = System.IO.Path.GetFileNameWithoutExtension(info.Name);
                    string kind = name.EndsWith("-output", StringComparison.Ordinal) ? "output" : "input";
                    string camera = relative.Contains('/') ? relative[..relative.IndexOf('/')] : string.Empty;
                    return new SnapshotEntry(camera, relative, kind, info.Length, info.LastWriteTimeUtc);
                })
                .OrderByDescending(entry => entry.CreatedUtc)
                .ToArray();
        }

        // The full path of a listed snapshot, or null when the path is not a .jpg inside the snapshot root that exists.
        public string? ResolveFile(string relativePath)
        {
            if (string.IsNullOrWhiteSpace(relativePath) || !relativePath.EndsWith(".jpg", StringComparison.OrdinalIgnoreCase)) return null;
            string root = System.IO.Path.GetFullPath(SnapshotRoot);
            string full = System.IO.Path.GetFullPath(System.IO.Path.Combine(root, relativePath.Replace('/', System.IO.Path.DirectorySeparatorChar)));
            bool inside = full.StartsWith(root + System.IO.Path.DirectorySeparatorChar, StringComparison.Ordinal);
            return inside && File.Exists(full) ? full : null;
        }

        public bool Delete(string relativePath)
        {
            string? full = ResolveFile(relativePath);
            if (full == null) return false;
            File.Delete(full);
            return true;
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
