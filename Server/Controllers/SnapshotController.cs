using System;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;

namespace Server.Controllers
{
    // The snapshots saved under snapshots/<node>/ (by the UI or a robot's NT snapshot request).
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

        // POST: save the latest frame of any node (camera, crop or detector): annotated for a detector, raw otherwise; null when it has none yet
        [HttpPost("snapshot/take")]
        public Task<string?> TakeNode([FromQuery] int NodeID)
        {
            return Task.Run(() => SnapshotService.Instance.SaveNode(NodeID));
        }
    }
}
