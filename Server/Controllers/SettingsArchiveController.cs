using System.IO;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers
{
    // Settings export and import (one ZIP). Importing replaces the saved data and restarts the server.
    internal class SettingsArchiveController : ControllerBase
    {
        private static SettingsArchive Archive => new SettingsArchive(DeviceData.Default, () => VersionInfo.Current().Server);

        // GET: a ZIP of the graph, settings, saved graph profiles, uploaded field layouts and calibrations. models=true adds the uploaded models
        // (large); log=true adds the server log.
        [HttpGet("device/settings/export")]
        public async Task<IActionResult> Export([FromQuery] bool models = false, [FromQuery] bool log = false)
        {
            var groups = new System.Collections.Generic.List<DataGroup> { DataGroup.Configuration, DataGroup.Calibrations };
            if (models) groups.Add(DataGroup.Models);

            // built in memory-friendly order: to a temporary file, then streamed
            string temporary = Path.GetTempFileName();
            await using (var file = new FileStream(temporary, FileMode.Create, FileAccess.ReadWrite))
            {
                Archive.Export(file, groups, log);
            }
            var stream = new FileStream(temporary, FileMode.Open, FileAccess.Read, FileShare.Read, 4096, FileOptions.DeleteOnClose);
            return File(stream, "application/zip", $"lumenvision-settings-{System.DateTime.UtcNow:yyyyMMdd-HHmmss}.zip");
        }

        // POST: the ZIP as the request body. Validated first (a bad archive changes nothing), then the data it carries replaces the saved data after a
        // backup into backups/, and the server restarts to load it.
        [HttpPost("device/settings/import")]
        [DisableRequestSizeLimit]
        public async Task<ImportResult> Import()
        {
            string temporary = Path.GetTempFileName();
            try
            {
                await using (var file = new FileStream(temporary, FileMode.Create, FileAccess.Write))
                {
                    await HttpContext.Request.Body.CopyToAsync(file, HttpContext.RequestAborted);
                }
                await using var archive = new FileStream(temporary, FileMode.Open, FileAccess.Read);
                ImportResult result = Archive.Import(archive);

                // the live state must not be saved over what was just restored
                DB.Instance.SuspendSaves();
                PowerService.Instance.RestartServer();
                return result;
            }
            finally
            {
                System.IO.File.Delete(temporary);
            }
        }
    }
}
