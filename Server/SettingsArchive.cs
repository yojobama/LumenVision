using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text.Json;
using Server.Web;

namespace Server
{
    public sealed record ArchiveManifest(int Format, string ServerVersion, DateTime CreatedUtc, string[] Groups, string[] Files);

    public sealed record ImportResult(int FilesRestored, string[] Groups, string? BackupPath);

    // Settings export and import as one ZIP: a manifest.json plus the files of the chosen DataGroups. Importing validates the manifest and every
    // entry name before touching anything, backs up what it replaces, and writes only files that belong to a known group inside the data folder.
    public sealed class SettingsArchive
    {
        public const int CurrentFormat = 1;
        public const string ManifestName = "manifest.json";
        private const int MaxEntries = 20000;
        private const long MaxUncompressedBytes = 4L * 1024 * 1024 * 1024;
        private const int BackupsKept = 5;

        // the groups an archive may carry and an import will restore (media is too large to move this way)
        private static readonly DataGroup[] Portable = { DataGroup.Configuration, DataGroup.Calibrations, DataGroup.Models };
        private const string LogFile = "DBLog.txt";

        private readonly DeviceData data;
        private readonly Func<string> serverVersion;

        public SettingsArchive(DeviceData data, Func<string> serverVersion)
        {
            this.data = data;
            this.serverVersion = serverVersion;
        }

        public string BackupDirectory => data.FullPath("backups");

        // ---- export ----

        // Writes the archive of the groups (Configuration and Calibrations by default; Models can be large) and optionally the server log.
        public void Export(Stream output, IEnumerable<DataGroup> groups, bool includeLog)
        {
            DataGroup[] chosen = groups.Where(g => Portable.Contains(g)).Distinct().ToArray();
            var files = chosen.SelectMany(g => data.Files(g)).Distinct().ToList();
            if (includeLog && File.Exists(data.FullPath(LogFile))) files.Add(LogFile);

            using var zip = new ZipArchive(output, ZipArchiveMode.Create, leaveOpen: true);
            var manifest = new ArchiveManifest(CurrentFormat, serverVersion(), DateTime.UtcNow, chosen.Select(g => g.ToString()).ToArray(), files.ToArray());
            ZipArchiveEntry manifestEntry = zip.CreateEntry(ManifestName);
            using (var writer = new StreamWriter(manifestEntry.Open()))
                writer.Write(JsonSerializer.Serialize(manifest, new JsonSerializerOptions { WriteIndented = true }));

            foreach (string relative in files)
            {
                ZipArchiveEntry entry = zip.CreateEntry(relative, CompressionLevel.Optimal);
                using Stream destination = entry.Open();
                using FileStream source = new(data.FullPath(relative), FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                source.CopyTo(destination);
            }
        }

        // ---- import ----

        // A manifest-listed name an import may write: a known file or something under a known folder, never the bundled layouts or the log.
        private static bool IsRestorable(string relative)
        {
            if (relative is "data.json" or "settings.json" or "calibrations.json" or "stereoCalibrations.json") return true;
            if (relative.StartsWith("graph-profiles/", StringComparison.Ordinal)) return relative.EndsWith(".json", StringComparison.Ordinal);
            if (relative.StartsWith("fieldLayouts/", StringComparison.Ordinal)) return !relative.StartsWith("fieldLayouts/bundled/", StringComparison.Ordinal);
            if (relative.StartsWith("models/", StringComparison.Ordinal)) return true;
            return false;
        }

        // Checks an archive without writing anything; returns its manifest or throws a 400 saying what is wrong.
        public ArchiveManifest Validate(ZipArchive zip)
        {
            if (zip.Entries.Count > MaxEntries) throw ApiException.BadRequest("the archive has too many files");
            if (zip.Entries.Sum(e => e.Length) > MaxUncompressedBytes) throw ApiException.BadRequest("the archive is too large");

            ZipArchiveEntry? manifestEntry = zip.GetEntry(ManifestName) ?? throw ApiException.BadRequest("this is not a LumenVision settings archive (no manifest.json)");
            ArchiveManifest? manifest;
            try
            {
                using var reader = new StreamReader(manifestEntry.Open());
                manifest = JsonSerializer.Deserialize<ArchiveManifest>(reader.ReadToEnd());
            }
            catch (JsonException)
            {
                throw ApiException.BadRequest("the archive's manifest is not valid");
            }
            if (manifest == null) throw ApiException.BadRequest("the archive's manifest is empty");
            if (manifest.Format != CurrentFormat) throw ApiException.BadRequest($"the archive format {manifest.Format} is not supported (expected {CurrentFormat})");

            var listed = new HashSet<string>(manifest.Files, StringComparer.Ordinal);
            foreach (ZipArchiveEntry entry in zip.Entries)
            {
                if (entry.FullName == ManifestName || entry.FullName == LogFile) continue;
                if (entry.FullName.EndsWith('/')) continue;
                if (!listed.Contains(entry.FullName)) throw ApiException.BadRequest($"'{entry.FullName}' is in the archive but not in its manifest");
                if (!IsRestorable(entry.FullName) || data.SafePath(entry.FullName) == null) throw ApiException.BadRequest($"'{entry.FullName}' is not something a settings import may write");
            }
            return manifest;
        }

        // Replaces the saved data with the archive's: validates, backs up the groups it replaces, then writes. The server must restart afterwards.
        public ImportResult Import(Stream archive)
        {
            ZipArchive zip;
            try
            {
                zip = new ZipArchive(archive, ZipArchiveMode.Read, leaveOpen: true);
            }
            catch (InvalidDataException)
            {
                throw ApiException.BadRequest("this is not a ZIP file");
            }
            using (zip)
            {
                ArchiveManifest manifest = Validate(zip);
                var entries = zip.Entries.Where(e => e.FullName != ManifestName && e.FullName != LogFile && !e.FullName.EndsWith('/')).ToList();

                // which groups the archive carries decides what gets replaced; whatever the archive lacks is left alone
                DataGroup[] replaced = Portable.Where(g => manifest.Groups.Contains(g.ToString())).ToArray();
                string? backup = null;
                if (replaced.Length > 0 && replaced.Any(g => data.Files(g).Any())) backup = WriteBackup(replaced);

                data.Delete(replaced);
                foreach (ZipArchiveEntry entry in entries)
                {
                    string full = data.SafePath(entry.FullName)!; // validated above
                    Directory.CreateDirectory(Path.GetDirectoryName(full)!);
                    string temporary = full + ".importing";
                    entry.ExtractToFile(temporary, overwrite: true);
                    File.Move(temporary, full, overwrite: true);
                }
                return new ImportResult(entries.Count, replaced.Select(g => g.ToString()).ToArray(), backup);
            }
        }

        private string WriteBackup(DataGroup[] groups)
        {
            Directory.CreateDirectory(BackupDirectory);
            string path = Path.Combine(BackupDirectory, $"before-import-{DateTime.UtcNow:yyyyMMdd-HHmmss-fff}.zip");
            using (var file = File.Create(path)) Export(file, groups, includeLog: false);

            // keep only the newest few
            foreach (string old in Directory.EnumerateFiles(BackupDirectory, "before-import-*.zip").OrderByDescending(f => f, StringComparer.Ordinal).Skip(BackupsKept))
            {
                try { File.Delete(old); } catch (IOException) { /* an old backup that cannot be deleted does no harm */ }
            }
            return path;
        }
    }
}
