using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using MsLogLevel = Microsoft.Extensions.Logging.LogLevel;

namespace Server
{
    // Feeds what the server prints (Console.Out/Error) into the LogHub, line by line, while still printing it (the journal keeps its copy).
    public sealed class ConsoleTee : TextWriter
    {
        private readonly TextWriter original;
        private readonly LogHub hub;
        private readonly LogSeverity level;
        private readonly StringBuilder line = new StringBuilder();

        public ConsoleTee(TextWriter original, LogHub hub, LogSeverity level)
        {
            this.original = original;
            this.hub = hub;
            this.level = level;
        }

        public override Encoding Encoding => original.Encoding;

        public override void Write(char value)
        {
            original.Write(value);
            lock (line)
            {
                if (value == '\n') Emit();
                else if (value != '\r') line.Append(value);
            }
        }

        public override void Write(string? value)
        {
            if (value == null) return;
            original.Write(value);
            lock (line)
            {
                foreach (char c in value)
                {
                    if (c == '\n') Emit();
                    else if (c != '\r') line.Append(c);
                }
            }
        }

        public override void Flush() => original.Flush();

        // ASP.NET's console logger prints "info: Category[0]" plus an indented message; those arrive through LogHubLoggerProvider instead
        // under systemd the console logger prints "<6>Category[0] message" (a syslog priority prefix) instead
        private static readonly System.Text.RegularExpressions.Regex SyslogPrefix = new(@"^<\d>", System.Text.RegularExpressions.RegexOptions.Compiled);

        private static bool IsFrameworkLoggerLine(string text) =>
            SyslogPrefix.IsMatch(text) || text.StartsWith("info: ", StringComparison.Ordinal) || text.StartsWith("warn: ", StringComparison.Ordinal) || text.StartsWith("fail: ", StringComparison.Ordinal)
            || text.StartsWith("crit: ", StringComparison.Ordinal) || text.StartsWith("dbug: ", StringComparison.Ordinal) || text.StartsWith("trce: ", StringComparison.Ordinal)
            || text.StartsWith("      ", StringComparison.Ordinal);

        private void Emit()
        {
            string text = line.ToString();
            line.Clear();
            if (text.Length == 0 || IsFrameworkLoggerLine(text)) return;
            // the server's own messages say when something failed; those stand out even though they were printed to stdout
            LogSeverity severity = level;
            if (severity == LogSeverity.Info && (text.Contains(" failed", StringComparison.OrdinalIgnoreCase) || text.Contains("error", StringComparison.OrdinalIgnoreCase)))
                severity = LogSeverity.Warning;
            hub.Add(severity, "server", text);
        }
    }

    // ILogger output (unhandled request errors, hosting messages) into the LogHub. The framework's own chatter is limited to warnings and worse.
    public sealed class LogHubLoggerProvider : ILoggerProvider
    {
        private readonly LogHub hub;

        public LogHubLoggerProvider(LogHub hub) => this.hub = hub;

        public ILogger CreateLogger(string categoryName) => new HubLogger(hub, categoryName);

        public void Dispose() { }

        private sealed class HubLogger : ILogger
        {
            private readonly LogHub hub;
            private readonly string category;
            private readonly bool framework;

            public HubLogger(LogHub hub, string category)
            {
                this.hub = hub;
                this.category = category;
                framework = category.StartsWith("Microsoft", StringComparison.Ordinal) || category.StartsWith("System", StringComparison.Ordinal);
            }

            public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;

            public bool IsEnabled(MsLogLevel logLevel) => logLevel >= (framework ? MsLogLevel.Warning : MsLogLevel.Information) && logLevel != MsLogLevel.None;

            public void Log<TState>(MsLogLevel logLevel, EventId eventId, TState state, Exception? exception, Func<TState, Exception?, string> formatter)
            {
                if (!IsEnabled(logLevel)) return;
                string message = formatter(state, exception);
                if (exception != null) message += $" ({exception.GetType().Name}: {exception.Message})";
                LogSeverity severity = logLevel switch
                {
                    MsLogLevel.Warning => LogSeverity.Warning,
                    MsLogLevel.Error or MsLogLevel.Critical => LogSeverity.Error,
                    _ => LogSeverity.Info,
                };
                hub.Add(severity, "server", $"{category}: {message}");
            }
        }
    }

    // Follows a plain log file that another component appends to (LumenCore's LumenVision.log, the store's DBLog.txt) and adds each new line to the
    // LogHub. Lines look like "[INFO]: text"; anything else counts as Info. Starts at the end of the file, so history is not replayed.
    public sealed class LogFileFollower : BackgroundService
    {
        private readonly LogHub hub;
        private readonly string path;
        private readonly string source;
        private readonly TimeSpan interval;
        private long position = -1;

        public LogFileFollower(LogHub hub, string path, string source, TimeSpan? interval = null)
        {
            this.hub = hub;
            this.path = path;
            this.source = source;
            this.interval = interval ?? TimeSpan.FromSeconds(1);
        }

        public static (LogSeverity Level, string Message) ParseLine(string line)
        {
            if (line.StartsWith('[') && line.IndexOf("]: ", StringComparison.Ordinal) is int close and > 0)
            {
                string tag = line[1..close].ToUpperInvariant();
                string message = line[(close + 3)..];
                switch (tag)
                {
                    case "INFO": return (LogSeverity.Info, message);
                    case "WARNING": case "WARN": return (LogSeverity.Warning, message);
                    case "ERROR": case "WTF": return (LogSeverity.Error, message);
                    case "DEBUG": return (LogSeverity.Debug, message);
                }
            }
            return (LogSeverity.Info, line);
        }

        // Reads what was appended since the last call (or nothing the first time); returns how many entries it added.
        public int Poll()
        {
            if (!File.Exists(path)) { position = -1; return 0; }
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            if (position < 0 || stream.Length < position)
            {
                // first look at the file, or it was truncated or replaced: start from its end (a replaced file from its start)
                bool replaced = position >= 0;
                position = replaced ? 0 : stream.Length;
                if (!replaced) return 0;
            }
            if (stream.Length == position) return 0;

            stream.Seek(position, SeekOrigin.Begin);
            using var reader = new StreamReader(stream, Encoding.UTF8);
            int added = 0;
            string? text;
            while ((text = reader.ReadLine()) != null)
            {
                if (text.Length == 0) continue;
                var (level, message) = ParseLine(text);
                hub.Add(level, source, message);
                added++;
            }
            position = stream.Length;
            return added;
        }

        protected override async Task ExecuteAsync(CancellationToken stoppingToken)
        {
            while (!stoppingToken.IsCancellationRequested)
            {
                try
                {
                    Poll();
                }
                catch (IOException)
                {
                    // the file is being rotated or is briefly locked; the next poll tries again
                }
                try
                {
                    await Task.Delay(interval, stoppingToken);
                }
                catch (OperationCanceledException)
                {
                    return;
                }
            }
        }
    }
}
