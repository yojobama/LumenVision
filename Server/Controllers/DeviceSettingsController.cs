using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers
{
    // The device-level settings kept in settings.json: how to reach the robot's NetworkTables server and the LED GPIO.
    internal class DeviceSettingsController : ControllerBase
    {
        [HttpGet("device/settings")]
        public Task<DeviceSettingsData> Get()
        {
            return Task.FromResult(DeviceSettings.Instance.Data);
        }

        // PUT: replace the settings; invalid ones are rejected with 400 and nothing changes. A NetworkTables change reconnects the NT sinks
        // and an LED change is picked up on the next LED command.
        [HttpPut("device/settings")]
        public async Task<DeviceSettingsData> Put()
        {
            string body = await HttpContext.GetRequestBodyAsStringAsync();
            DeviceSettingsData? next;
            try
            {
                next = System.Text.Json.JsonSerializer.Deserialize<DeviceSettingsData>(body);
            }
            catch (System.Text.Json.JsonException)
            {
                throw ApiException.BadRequest("the body is not valid settings JSON");
            }
            if (next == null) throw ApiException.BadRequest("the body is empty");
            string? problem = DeviceSettings.Validate(next);
            if (problem != null) throw ApiException.BadRequest(problem);

            NetworkTablesSettings before = DeviceSettings.Instance.Data.NetworkTables;
            bool ntChanged = System.Text.Json.JsonSerializer.Serialize(before) != System.Text.Json.JsonSerializer.Serialize(next.NetworkTables);
            DeviceSettings.Instance.Replace(next);
            if (ntChanged) SinkManager.Instance.ReapplyNetworkTablesSettings();
            LedController.Instance.SettingsChanged();
            return next;
        }
    }
}
