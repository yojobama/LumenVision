using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers.sources
{
    internal class SourceController : ControllerBase
    {
        // GET: Source activation status
        [HttpGet("source/isActive")]
        public Task<bool> IsActive([FromQuery] int SourceID)
        {
            bool status = SourceManager.Instance.IsSourceActive(SourceID);
            return Task.FromResult(status);
        }

        // GET: All registered sources;
        [HttpGet("source/getAll")]
        public Task<Source[]> GetAllSources()
        {
            List<Source> sources = new List<Source>();
            
            foreach (var item in SourceManager.Instance.GetAllSourceIds())
                sources.Add(SourceManager.Instance.GetSourceById(item));
            
            return Task.FromResult(sources.ToArray());
        }

        // PATCH: Rename an ImageFile source;
        [HttpPatch("source/rename")]
        public Task Rename([FromQuery] int SourceID, [FromQuery] string newName)
        {
            SourceManager.Instance.ChangeSourceName(SourceID, newName);
            return Task.CompletedTask;
        }

        // DELETE: delete a source;
        [HttpDelete("source/delete")]
        public Task Delete([FromQuery] int SourceID)
        {
            SourceManager.Instance.DeleteSource(SourceID);
            return Task.CompletedTask;
        }

        // POST: save the source's latest frame under snapshots/ (fileName only, no path). Returns false if no frame yet.
        [HttpPost("source/snapshot")]
        public Task<bool> SaveSnapshot([FromQuery] int SourceID, [FromQuery] string fileName)
        {
            string snapshotDir = Path.Combine(AppContext.BaseDirectory, "snapshots");
            Directory.CreateDirectory(snapshotDir);
            string safeFileName = Path.GetFileName(fileName); // strips any directory components a caller tried to sneak in
            string fullPath = Path.Combine(snapshotDir, safeFileName);
            bool saved = ManagerWrapper.Instance.SaveSnapshot(SourceID, fullPath);
            return Task.FromResult(saved);
        }
    }
}
