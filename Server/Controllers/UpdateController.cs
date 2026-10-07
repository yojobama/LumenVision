using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers
{
    // Offline updates: upload a lumenvision-backend .deb, then install it. The install runs as root in a separate systemd unit and restarts the server.
    internal class UpdateController : ControllerBase
    {
        // POST: the .deb as the request body (application/octet-stream). It is checked and kept, not installed.
        [HttpPost("device/update/upload")]
        [DisableRequestSizeLimit]
        public async Task<StagedPackage> Upload()
        {
            return await UpdateService.Instance.StageAsync(HttpContext.Request.Body);
        }

        // POST: install the uploaded package. Returns at once; follow it with GET device/update/status. The server restarts when it finishes.
        [HttpPost("device/update/install")]
        public async Task Install()
        {
            await UpdateService.Instance.StartAsync();
        }

        // GET: idle, running, succeeded or failed, with the tail of the installer's output
        [HttpGet("device/update/status")]
        public Task<UpdateStatus> Status()
        {
            return UpdateService.Instance.StatusAsync();
        }
    }
}
