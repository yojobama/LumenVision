using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers
{
    // Restart, reboot, power off and factory reset. These run privileged commands (see PrivilegedCommand) a second after they return.
    internal class PowerController : ControllerBase
    {
        public record FactoryResetRequest(string Confirmation, bool KeepCalibrations, bool KeepModels, bool KeepMedia);

        // POST: restart the LumenVision server (the board stays up)
        [HttpPost("device/restart")]
        public Task Restart()
        {
            PowerService.Instance.RestartServer();
            return Task.CompletedTask;
        }

        // POST: reboot the board
        [HttpPost("device/reboot")]
        public Task Reboot()
        {
            PowerService.Instance.Reboot();
            return Task.CompletedTask;
        }

        // POST: power the board off; it must be switched on by hand afterwards
        [HttpPost("device/shutdown")]
        public Task Shutdown()
        {
            PowerService.Instance.PowerOff();
            return Task.CompletedTask;
        }

        // POST: delete the saved graph, settings and uploaded layouts (and, unless kept, calibrations, models and media), then restart.
        // The body must carry Confirmation "factory reset". Returns how many files were deleted.
        [HttpPost("device/factoryReset")]
        public async Task<int> FactoryReset()
        {
            string body = await HttpContext.GetRequestBodyAsStringAsync();
            FactoryResetRequest? request;
            try
            {
                request = System.Text.Json.JsonSerializer.Deserialize<FactoryResetRequest>(body);
            }
            catch (System.Text.Json.JsonException)
            {
                throw ApiException.BadRequest("the body is not valid JSON for this request");
            }
            if (request == null) throw ApiException.BadRequest("the body is empty");
            return PowerService.Instance.FactoryReset(request.Confirmation, request.KeepCalibrations, request.KeepModels, request.KeepMedia);
        }
    }
}
