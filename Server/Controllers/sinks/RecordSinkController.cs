using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers.sinks
{
    public class RecordSegmentDto
    {
        public string FileName { get; set; } = "";
        public long SizeBytes { get; set; }
        public DateTime LastWriteTimeUtc { get; set; }
    }

    // Recording as segmented MP4 plus a JSON-Lines telemetry sidecar per segment (see RecordSink.h).
    internal class RecordSinkController : ControllerBase
    {
        // POST: create a RecordSink; bind it afterwards (PATCH /sink/bind). dstFolder null = derived from the sink name.
        // Numeric parameters are nullable and defaulted in the body via ??, since an omitted [FromQuery] value type binds 0.
        [HttpPost("recordSink/create")]
        public Task<int> Create([FromQuery] string name, [FromQuery] string? dstFolder = null,
            [FromQuery] string? encoderName = null, [FromQuery] int? bitrateKbps = null,
            [FromQuery] int? fps = null, [FromQuery] int? segmentSeconds = null,
            [FromQuery] long? maxFolderSizeBytes = null, [FromQuery] int? maxFileCount = null)
        {
            int sinkId = SinkManager.Instance.AddRecordSink(name, dstFolder, encoderName,
                bitrateKbps ?? 8000, fps ?? 30, segmentSeconds ?? 300, maxFolderSizeBytes ?? 0, maxFileCount ?? 0);
            return Task.FromResult(sinkId);
        }

        // GET: recorded segments, newest first, with size and last-write time (video duration is not probed)
        [HttpGet("recordSink/{id}/segments")]
        public Task<List<RecordSegmentDto>> GetSegments(int id)
        {
            var sink = RequireRecordSink(id);
            var result = new List<RecordSegmentDto>();
            foreach (var filename in SinkManager.Instance.GetRecordSinkSegments(id))
            {
                if (!TryResolveSegmentPath(sink.RecordDstFolder!, filename, out string fullPath)) continue;
                var info = new FileInfo(fullPath);
                if (!info.Exists) continue;
                result.Add(new RecordSegmentDto { FileName = filename, SizeBytes = info.Length, LastWriteTimeUtc = info.LastWriteTimeUtc });
            }
            return Task.FromResult(result);
        }

        // GET: download one segment (video or .jsonl sidecar); `file` is resolved only within this sink's RecordDstFolder.
        // PhysicalFile with range processing lets clients seek and resume.
        [HttpGet("recordSink/{id}/download")]
        public Task<IActionResult> Download(int id, [FromQuery] string file)
        {
            var sink = RequireRecordSink(id);
            if (!TryResolveSegmentPath(sink.RecordDstFolder!, file, out string fullPath) || !System.IO.File.Exists(fullPath))
            {
                throw ApiException.NotFound();
            }
            string contentType = Path.GetExtension(fullPath) == ".jsonl" ? "application/x-ndjson" : "video/mp4";
            IActionResult result = PhysicalFile(Path.GetFullPath(fullPath), contentType, Path.GetFileName(fullPath), enableRangeProcessing: true);
            return Task.FromResult(result);
        }

        // POST: use an already-recorded segment as a VideoFileSource, without re-uploading.
        // POST: start (enabled=true) or stop (enabled=false) recording on every source; returns the running RecordSink count.
        // The robot does the same via NT <root>/config/recording (SinkManager.SetAllRecording).
        [HttpPost("recordSink/all")]
        public Task<int> SetAllRecording([FromQuery] bool enabled)
        {
            return Task.FromResult(SinkManager.Instance.SetAllRecording(enabled));
        }

        // GET: whether any RecordSink is currently running
        [HttpGet("recordSink/all")]
        public Task<bool> IsAnyRecording()
        {
            return Task.FromResult(SinkManager.Instance.IsAnyRecording());
        }

        [HttpPost("recordSink/{id}/promote")]
        public Task<int> Promote(int id, [FromQuery] string file, [FromQuery] string? name = null)
        {
            var sink = RequireRecordSink(id);
            if (!TryResolveSegmentPath(sink.RecordDstFolder!, file, out string fullPath) || !System.IO.File.Exists(fullPath))
            {
                throw ApiException.NotFound();
            }
            int sourceId = SourceManager.Instance.InitializeVideoFileSource(fullPath, 30, name ?? Path.GetFileNameWithoutExtension(file));
            return Task.FromResult(sourceId);
        }

        // DELETE: remove one segment and its .jsonl sidecar
        [HttpDelete("recordSink/{id}/segments")]
        public Task<bool> DeleteSegment(int id, [FromQuery] string file)
        {
            RequireRecordSink(id);
            return Task.FromResult(SinkManager.Instance.DeleteRecordSinkSegment(id, file));
        }

        private static Sink RequireRecordSink(int id)
        {
            var sink = SinkManager.Instance.GetSinkById(id);
            if (sink == null || sink.Type != SinkType.RecordSink || string.IsNullOrEmpty(sink.RecordDstFolder))
            {
                throw ApiException.NotFound();
            }
            return sink;
        }

        // filename must be a bare name (no separators or "..") and resolve inside dstFolder once normalised;
        // both checks are needed to prevent path traversal.
        private static bool TryResolveSegmentPath(string dstFolder, string filename, out string fullPath)
        {
            fullPath = "";
            if (string.IsNullOrEmpty(filename) || filename.Contains("..") || filename.Contains('/') || filename.Contains('\\'))
            {
                return false;
            }
            string folderFull = Path.GetFullPath(dstFolder);
            string candidate = Path.GetFullPath(Path.Combine(folderFull, filename));
            if (!candidate.StartsWith(folderFull + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }
            fullPath = candidate;
            return true;
        }
    }
}
