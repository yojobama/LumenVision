using System;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;

namespace Server.Controllers
{
    // The snapshots saved under snapshots/<camera>/ (by the UI or a robot's NT snapshot request).
    internal class SnapshotController : ControllerBase
    {
        // GET: every saved snapshot, newest first
        [HttpGet("snapshot/list")]
        public Task<SnapshotEntry[]> List()
        {
            return Task.FromResult(SnapshotService.Instance.List());
        }

        // GET: the image of one listed snapshot (Path as returned by snapshot/list)
        [HttpGet("snapshot/file")]
        public IActionResult Download([FromQuery] string Path)
        {
            string? full = SnapshotService.Instance.ResolveFile(Path);
            return full == null ? NotFound() : PhysicalFile(full, "image/jpeg");
        }

        // DELETE: remove one snapshot; false when it does not exist
        [HttpDelete("snapshot")]
        public Task<bool> Delete([FromQuery] string Path)
        {
            return Task.FromResult(SnapshotService.Instance.Delete(Path));
        }

        // POST: save a snapshot of a camera now (Kind is "input" for the raw frame or "output" for the detector's annotated frame).
        // Returns the new snapshot's path, or null when the camera has no frame yet.
        [HttpPost("snapshot/take")]
        public Task<string?> Take([FromQuery] int SourceID, [FromQuery] string Kind = "input")
        {
            SnapshotKind kind = string.Equals(Kind, "output", StringComparison.OrdinalIgnoreCase) ? SnapshotKind.Output : SnapshotKind.Input;
            return Task.Run(() => SnapshotService.Instance.Save(SourceID, kind));
        }
    }
}
