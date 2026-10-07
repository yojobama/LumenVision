using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace Server
{
    // How much log history the device keeps. The numbers are the user's (Settings -> Logs); LogRetention applies them to LumenCore's own logs
    // (rotation by size) and the server's daily files (size and day limits).
    public sealed class LogSettings
    {
        // a log file rotates once it reaches this size
        public int MaxFileMb { get; set; } = 10;
        // rotated copies of LumenCore's and the store's log files kept next to the live file
        public int FilesKept { get; set; } = 3;
        // everything under logs/ together
        public int ServerBudgetMb { get; set; } = 50;
        public int KeepDays { get; set; } = 7;
    }

    // Disk use of each kind of log, in bytes
    public sealed record LogUsage(long CoreBytes, long DatabaseBytes, long ServerBytes, long TotalBytes, int Files);

    public static class LogRetention
    {
        public static string? Validate(LogSettings s)
        {
            if (s.MaxFileMb is < 1 or > 1024) return "the log file size must be 1 to 1024 MB";
            if (s.FilesKept is < 0 or > 20) return "the number of rotated log files kept must be 0 to 20";
            if (s.ServerBudgetMb < s.MaxFileMb || s.ServerBudgetMb > 10240) return "the server log budget must be at least one log file and at most 10240 MB";
            if (s.KeepDays is < 1 or > 365) return "logs are kept for 1 to 365 days";
            return null;
        }

        public static LogLimits ServerLimits(LogSettings s) => new(s.MaxFileMb * 1024L * 1024, s.ServerBudgetMb * 1024L * 1024, s.KeepDays);

        // Puts the settings into effect: LumenCore's loggers (every log file the native side writes) and the server's own folder.
        public static void Apply(LogSettings settings, LogHub hub)
        {
            hub.File.Configure(ServerLimits(settings));
            try
            {
                ManagerWrapper.Instance.SetLogRotation(settings.MaxFileMb * 1024L * 1024, settings.FilesKept);
            }
            catch (Exception ex)
            {
                // the native library is not loaded (a tool or test without it): the server's own files are still limited
                Console.WriteLine($"Setting the native log rotation failed: {ex.Message}");
            }
        }

        // a native log and its rotated copies: name, name.1, name.2 ...
        private static IEnumerable<string> WithRotations(string root, string name)
        {
            if (File.Exists(Path.Combine(root, name))) yield return Path.Combine(root, name);
            if (!Directory.Exists(root)) yield break;
            foreach (string file in Directory.EnumerateFiles(root, name + ".*"))
            {
                string fileName = Path.GetFileName(file);
                // the pattern also matches the live file itself on some systems; a rotated copy is "<name>.<number>"
                if (fileName.Length <= name.Length + 1) continue;
                string suffix = fileName[(name.Length + 1)..];
                if (suffix.All(char.IsDigit)) yield return file;
            }
        }

        // LumenCore's and the store's log files with their rotated copies
        public static IEnumerable<string> NativeFiles(string root) => WithRotations(root, "LumenVision.log").Concat(WithRotations(root, "DBLog.txt"));

        private static long Size(IEnumerable<string> files) => files.Sum(f => new FileInfo(f).Length);

        public static LogUsage Usage(string root, LogHub hub)
        {
            var core = WithRotations(root, "LumenVision.log").ToList();
            var database = WithRotations(root, "DBLog.txt").ToList();
            var server = hub.File.Files().ToList();
            long coreBytes = Size(core), databaseBytes = Size(database), serverBytes = Size(server);
            return new LogUsage(coreBytes, databaseBytes, serverBytes, coreBytes + databaseBytes + serverBytes, core.Count + database.Count + server.Count);
        }

        // Deletes every log: the server's files and rotated copies outright, and empties (rather than deletes) the files LumenCore holds open.
        public static int Clear(string root, LogHub hub)
        {
            int cleared = hub.File.DeleteAll();
            foreach (string name in new[] { "LumenVision.log", "DBLog.txt" })
            {
                foreach (string file in WithRotations(root, name))
                {
                    try
                    {
                        if (Path.GetFileName(file) == name)
                        {
                            using var stream = new FileStream(file, FileMode.Open, FileAccess.Write, FileShare.ReadWrite | FileShare.Delete);
                            stream.SetLength(0);
                        }
                        else
                        {
                            File.Delete(file);
                        }
                        cleared++;
                    }
                    catch (IOException)
                    {
                        // held by something else; the next clear gets it
                    }
                }
            }
            hub.ClearMemory();
            return cleared;
        }
    }
}
