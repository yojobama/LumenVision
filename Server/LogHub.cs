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

    // What the server's own log files may take up: no file bigger than MaxFileBytes (a day rolls into numbered parts), the folder no bigger than
    // TotalBytes (the oldest files go first) and nothing older than KeepDays.
    public sealed record LogLimits(long MaxFileBytes, long TotalBytes, int KeepDays)
    {
        public static LogLimits Default { get; } = new(10L * 1024 * 1024, 50L * 1024 * 1024, 7);
    }

    // Daily log files under a folder: logs/lumenvision-yyyyMMdd-001.log, -002.log, ... as a day outgrows MaxFileBytes. Names sort in time order.
    public sealed class RollingLogFile
    {
        private const string Prefix = "lumenvision-";

        private readonly string directory;
        private readonly object sync = new object();
        private LogLimits limits;
        private string? currentDay;
        private int currentPart;
        private StreamWriter? writer;

        public RollingLogFile(string directory, int keepDays = 7) : this(directory, LogLimits.Default with { KeepDays = keepDays }) { }

        public RollingLogFile(string directory, LogLimits limits)
        {
            this.directory = directory;
            this.limits = limits;
        }

        public LogLimits Limits
        {
            get { lock (sync) return limits; }
        }

        // New limits apply from the next entry; files already over them are trimmed at once.
        public void Configure(LogLimits next)
        {
            lock (sync)
            {
                limits = next;
                if (System.IO.Directory.Exists(directory)) Trim(DateTime.UtcNow);
            }
        }

        private string PathOf(string day, int part) => Path.Combine(directory, $"{Prefix}{day}-{part:D3}.log");

        // the highest part number already on disk for a day (0 when there is none)
        private int HighestPart(string day)
        {
            int highest = 0;
            foreach (string file in Files())
            {
                string name = Path.GetFileNameWithoutExtension(file)[Prefix.Length..];
                if (name.Length == 12 && name.StartsWith(day, StringComparison.Ordinal) && int.TryParse(name[9..], out int part)) highest = Math.Max(highest, part);
            }
            return highest;
        }

        public void Write(LogEntry entry)
        {
            lock (sync)
            {
                try
                {
                    string day = entry.TimeUtc.ToString("yyyyMMdd");
                    if (writer == null || day != currentDay) Open(day, entry.TimeUtc);
                    writer!.WriteLine(entry.ToLine());
                    if (writer.BaseStream.Length >= limits.MaxFileBytes)
                    {
                        // the next entry goes to a new part; this is also when the folder's total is checked
                        writer.Dispose();
                        writer = null;
                        currentPart++;
                        Trim(entry.TimeUtc);
                    }
                }
                catch (IOException)
                {
                    // a full or read-only disk must not take the server down with it; the entry stays available in memory
                    writer?.Dispose();
                    writer = null;
                }
            }
        }

        private void Open(string day, DateTime now)
        {
            writer?.Dispose();
            System.IO.Directory.CreateDirectory(directory);
            if (day != currentDay)
            {
                currentDay = day;
                // continue the day's last part after a restart, unless that one is already full
                currentPart = Math.Max(1, HighestPart(day));
            }
            string path = PathOf(day, currentPart);
            if (File.Exists(path) && new FileInfo(path).Length >= limits.MaxFileBytes) path = PathOf(day, ++currentPart);
            writer = new StreamWriter(new FileStream(path, FileMode.Append, FileAccess.Write, FileShare.ReadWrite), new UTF8Encoding(false)) { AutoFlush = true };
            Trim(now);
        }

        // Deletes files older than KeepDays, then the oldest ones until the folder fits its budget. The file being written is never deleted.
        private void Trim(DateTime now)
        {
            string? open = writer != null ? PathOf(currentDay!, currentPart) : null;
            foreach (string file in Files().ToList())
            {
                string name = Path.GetFileNameWithoutExtension(file)[Prefix.Length..];
                if (name.Length >= 8 && DateTime.TryParseExact(name[..8], "yyyyMMdd", null,
                        System.Globalization.DateTimeStyles.AssumeUniversal | System.Globalization.DateTimeStyles.AdjustToUniversal, out DateTime day)
                    && now - day > TimeSpan.FromDays(limits.KeepDays) && file != open)
                {
                    TryDelete(file);
                }
            }

            var remaining = Files().Select(f => new FileInfo(f)).ToList();
            long total = remaining.Sum(f => f.Length);
            foreach (FileInfo oldest in remaining)
            {
                if (total <= limits.TotalBytes) break;
                if (oldest.FullName == open) continue;
                total -= oldest.Length;
                TryDelete(oldest.FullName);
            }
        }

        private static void TryDelete(string file)
        {
            try { File.Delete(file); } catch (IOException) { /* gone next time */ }
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

        // Closes the file being written and deletes every file in the folder; the next entry starts a new one.
        public int DeleteAll()
        {
            lock (sync)
            {
                writer?.Dispose();
                writer = null;
                currentDay = null;
                int deleted = 0;
                foreach (string file in Files().ToList())
                {
                    TryDelete(file);
                    deleted++;
                }
                return deleted;
            }
        }

        public long TotalBytes() => Files().Sum(f => new FileInfo(f).Length);

        public IEnumerable<string> Files() =>
            System.IO.Directory.Exists(directory) ? System.IO.Directory.EnumerateFiles(directory, Prefix + "*.log").OrderBy(f => f, StringComparer.Ordinal) : Enumerable.Empty<string>();
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

        // persist = false keeps the entry in memory and the live feed only: it was read from a file that already exists on disk
        public LogEntry Add(LogSeverity level, string source, string message, DateTime? timeUtc = null, bool persist = true)
        {
            LogEntry entry;
            lock (sync)
            {
                entry = new LogEntry(nextId++, timeUtc ?? DateTime.UtcNow, level, source, message.TrimEnd());
                recent.AddLast(entry);
                while (recent.Count > capacity) recent.RemoveFirst();
            }
            if (persist) file.Write(entry);
            Added?.Invoke(entry);
            return entry;
        }

        public void ClearMemory()
        {
            lock (sync) recent.Clear();
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
