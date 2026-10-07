using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers
{
    // The device's logs: the server's own output, LumenCore's log and the store's log, gathered by the LogHub and kept in daily files under logs/.
    internal class LogController : ControllerBase
    {
        private static LogSeverity ParseLevel(string? level)
        {
            if (string.IsNullOrEmpty(level)) return LogSeverity.Debug;
            if (Enum.TryParse(level, ignoreCase: true, out LogSeverity parsed)) return parsed;
            throw ApiException.BadRequest("level must be debug, info, warning or error");
        }

        private static DateTime? ParseSince(string? since)
        {
            if (string.IsNullOrEmpty(since)) return null;
            if (DateTime.TryParse(since, null, System.Globalization.DateTimeStyles.AdjustToUniversal | System.Globalization.DateTimeStyles.AssumeUniversal, out DateTime parsed)) return parsed;
            throw ApiException.BadRequest("since must be an ISO 8601 time, such as 2026-10-07T12:00:00Z");
        }

        // GET: the newest `lines` entries (1-5000, default 200) as text lines, oldest first, optionally limited to a minimum level and to entries since a
        // time. Empty (not an error) when there are none.
        [HttpGet("log/tail")]
        public Task<string[]> Tail([FromQuery] int lines = 200, [FromQuery] string? level = null, [FromQuery] string? since = null)
        {
            lines = Math.Clamp(lines, 1, 5000);
            return Task.FromResult(LogHub.Instance.Recent(lines, ParseLevel(level), ParseSince(since)).Select(e => e.ToLine()).ToArray());
        }

        // GET: the same entries as objects (Id, TimeUtc, Level, Source, Message); Level is a number: 0 debug, 1 info, 2 warning, 3 error
        [HttpGet("log/entries")]
        public Task<LogEntry[]> Entries([FromQuery] int lines = 200, [FromQuery] string? level = null, [FromQuery] string? since = null)
        {
            lines = Math.Clamp(lines, 1, 5000);
            return Task.FromResult(LogHub.Instance.Recent(lines, ParseLevel(level), ParseSince(since)).ToArray());
        }

        // GET: every log file as one ZIP (the daily files under logs/ plus LumenCore's and the store's own logs)
        [HttpGet("log/download")]
        public async Task<IActionResult> Download()
        {
            string temporary = Path.GetTempFileName();
            await using (var file = new FileStream(temporary, FileMode.Create, FileAccess.ReadWrite))
            using (var zip = new ZipArchive(file, ZipArchiveMode.Create, leaveOpen: false))
            {
                foreach (string path in LogHub.Instance.File.Files().Concat(new[] { "LumenVision.log", "DBLog.txt" }.Where(System.IO.File.Exists)))
                {
                    ZipArchiveEntry entry = zip.CreateEntry(Path.GetFileName(path), CompressionLevel.Optimal);
                    await using Stream destination = entry.Open();
                    await using var source = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                    await source.CopyToAsync(destination);
                }
            }
            var stream = new FileStream(temporary, FileMode.Open, FileAccess.Read, FileShare.Read, 4096, FileOptions.DeleteOnClose);
            return File(stream, "application/zip", $"lumenvision-logs-{DateTime.UtcNow:yyyyMMdd-HHmmss}.zip");
        }
    }
}
