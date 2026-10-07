using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace Server
{
    public enum LogSeverity
    {
        Debug = 0,
        Info = 1,
        Warning = 2,
        Error = 3,
    }

    // Source is where it came from: "server" (this process), "core" (LumenCore's own log) or "db" (the pipeline store's log).
    public sealed record LogEntry(long Id, DateTime TimeUtc, LogSeverity Level, string Source, string Message)
    {
        // "2026-10-07T12:00:00.123Z [INFO] core: message"
        public string ToLine() => $"{TimeUtc:yyyy-MM-ddTHH:mm:ss.fffZ} [{Level.ToString().ToUpperInvariant()}] {Source}: {Message}";
    }

    // Daily log files under a folder (logs/lumenvision-yyyyMMdd.log), oldest deleted after `keepDays`.
    public sealed class RollingLogFile
    {
        private readonly string directory;
        private readonly int keepDays;
        private readonly object sync = new object();
        private string? currentDay;
        private StreamWriter? writer;

        public RollingLogFile(string directory, int keepDays = 7)
        {
            this.directory = directory;
            this.keepDays = keepDays;
        }

        public void Write(LogEntry entry)
        {
            lock (sync)
            {
                try
                {
                    string day = entry.TimeUtc.ToString("yyyyMMdd");
                    if (day != currentDay || writer == null)
                    {
                        writer?.Dispose();
                        System.IO.Directory.CreateDirectory(directory);
                        writer = new StreamWriter(new FileStream(Path.Combine(directory, $"lumenvision-{day}.log"), FileMode.Append, FileAccess.Write, FileShare.ReadWrite), new UTF8Encoding(false))
                        {
                            AutoFlush = true,
                        };
                        currentDay = day;
                        DeleteOld(entry.TimeUtc);
                    }
                    writer.WriteLine(entry.ToLine());
                }
                catch (IOException)
                {
                    // a full or read-only disk must not take the server down with it; the entry stays available in memory
                    writer = null;
                }
            }
        }

        private void DeleteOld(DateTime now)
        {
            foreach (string file in System.IO.Directory.EnumerateFiles(directory, "lumenvision-*.log"))
            {
                string name = Path.GetFileNameWithoutExtension(file)["lumenvision-".Length..];
                if (DateTime.TryParseExact(name, "yyyyMMdd", null, System.Globalization.DateTimeStyles.AssumeUniversal | System.Globalization.DateTimeStyles.AdjustToUniversal, out DateTime day)
                    && now - day > TimeSpan.FromDays(keepDays))
                {
                    try { File.Delete(file); } catch (IOException) { /* gone next time */ }
                }
            }
        }

        // releases the open file (tests, and a clean shutdown)
        public void Close()
        {
            lock (sync)
            {
                writer?.Dispose();
                writer = null;
                currentDay = null;
            }
        }

        public IEnumerable<string> Files() =>
            System.IO.Directory.Exists(directory) ? System.IO.Directory.EnumerateFiles(directory, "lumenvision-*.log").OrderBy(f => f, StringComparer.Ordinal) : Enumerable.Empty<string>();
    }

    // Every log line of the device in one place: the server's own output, LumenCore's log and the store's log. Keeps the most recent entries in
    // memory for the UI and its live feed, and writes all of them to the rolling file that a log download collects.
    public sealed class LogHub
    {
        public static LogHub Instance { get; } = new LogHub(new RollingLogFile("logs"), 5000);

        private readonly RollingLogFile file;
        private readonly int capacity;
        private readonly LinkedList<LogEntry> recent = new LinkedList<LogEntry>();
        private readonly object sync = new object();
        private long nextId = 1;

        public event Action<LogEntry>? Added;

        public LogHub(RollingLogFile file, int capacity)
        {
            this.file = file;
            this.capacity = capacity;
        }

        public RollingLogFile File => file;

        public LogEntry Add(LogSeverity level, string source, string message, DateTime? timeUtc = null)
        {
            LogEntry entry;
            lock (sync)
            {
                entry = new LogEntry(nextId++, timeUtc ?? DateTime.UtcNow, level, source, message.TrimEnd());
                recent.AddLast(entry);
                while (recent.Count > capacity) recent.RemoveFirst();
            }
            file.Write(entry);
            Added?.Invoke(entry);
            return entry;
        }

        // The newest `max` entries at or above `minimum` and no older than `since`, oldest first.
        public List<LogEntry> Recent(int max, LogSeverity minimum = LogSeverity.Debug, DateTime? since = null)
        {
            lock (sync)
            {
                var matching = recent.Where(e => e.Level >= minimum && (since == null || e.TimeUtc >= since)).ToList();
                return matching.Count <= max ? matching : matching.GetRange(matching.Count - max, max);
            }
        }
    }
}
