using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace Server
{
    // Which parts of the device's saved data an operation covers.
    public enum DataGroup
    {
        // the graph (data.json), device settings, saved graph profiles and field layouts uploaded for detectors
        Configuration,
        Calibrations,
        Models,
        // uploaded images and videos, recordings and snapshots
        Media,
    }

    // Where the server keeps its files, relative to a root (the working directory in production, a temporary folder in tests). Factory reset,
    // export and import all go through this, so they agree on what each group contains.
    public sealed class DeviceData
    {
        public static DeviceData Default { get; } = new DeviceData(Directory.GetCurrentDirectory());

        public string Root { get; }

        public DeviceData(string root) => Root = root;

        private static readonly string[] ConfigurationFiles = { "data.json", "settings.json" };
        private static readonly string[] CalibrationFiles = { "calibrations.json", "stereoCalibrations.json" };

        // A path inside the group, relative to the root with forward slashes (the form used inside export archives).
        public IEnumerable<string> Files(DataGroup group)
        {
            IEnumerable<string> relative = group switch
            {
                DataGroup.Configuration => ConfigurationFiles
                    .Concat(FilesUnder("graph-profiles"))
                    // uploaded layouts only: fieldLayouts/bundled ships with the server
                    .Concat(FilesUnder("fieldLayouts").Where(f => !f.StartsWith("fieldLayouts/bundled/", StringComparison.Ordinal))),
                DataGroup.Calibrations => CalibrationFiles,
                DataGroup.Models => FilesUnder("models"),
                DataGroup.Media => FilesUnder("images").Concat(FilesUnder("videos")).Concat(FilesUnder("recordings")).Concat(FilesUnder("snapshots")),
                _ => Array.Empty<string>(),
            };
            return relative.Where(r => File.Exists(FullPath(r)));
        }

        public string FullPath(string relative) => Path.Combine(Root, relative.Replace('/', Path.DirectorySeparatorChar));

        // like FullPath, but null unless the result stays inside the root (an archive entry could otherwise name ../anything)
        public string? SafePath(string relative)
        {
            if (string.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative) || relative.Contains(':')) return null;
            string full = Path.GetFullPath(FullPath(relative));
            string root = Path.GetFullPath(Root);
            return full.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.Ordinal) ? full : null;
        }

        private IEnumerable<string> FilesUnder(string directory)
        {
            string full = FullPath(directory);
            if (!Directory.Exists(full)) return Array.Empty<string>();
            return Directory.EnumerateFiles(full, "*", SearchOption.AllDirectories)
                .Select(f => Path.GetRelativePath(Root, f).Replace('\\', '/'));
        }

        // Deletes every file of the groups; returns how many went. Empty folders are left in place (the server recreates what it needs).
        public int Delete(IEnumerable<DataGroup> groups)
        {
            int deleted = 0;
            foreach (string relative in groups.SelectMany(g => Files(g)).Distinct().ToList())
            {
                File.Delete(FullPath(relative));
                deleted++;
            }
            return deleted;
        }
    }
}
