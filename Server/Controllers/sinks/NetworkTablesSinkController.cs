using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers.sinks
{
    internal class NetworkTablesSinkController : ControllerBase
    {
        // POST: Create a NetworkTables sink that connects via team number
        [HttpPost("networkTablesSink/createForTeam")]
        public Task<int> CreateForTeam([FromQuery] string name, [FromQuery] int teamNumber,
            [FromQuery] string rootTable = "lumenvision", [FromQuery] string clientIdentity = "lumenvision")
        {
            int sinkId = SinkManager.Instance.AddNetworkTablesSinkForTeam(name, teamNumber, rootTable, clientIdentity);
            return Task.FromResult(sinkId);
        }

        // POST: Create a NetworkTables sink that connects to an explicit server address (bench testing)
        [HttpPost("networkTablesSink/createForServer")]
        public Task<int> CreateForServer([FromQuery] string name, [FromQuery] string serverAddress,
            [FromQuery] int port = 0, [FromQuery] string rootTable = "lumenvision", [FromQuery] string clientIdentity = "lumenvision")
        {
            int sinkId = SinkManager.Instance.AddNetworkTablesSinkForServer(name, serverAddress, port, rootTable, clientIdentity);
            return Task.FromResult(sinkId);
        }

        // POST: Create a NetworkTablesSink that connects the way the device's Settings say (GET/PUT /api/device/settings)
        [HttpPost("networkTablesSink/createFromSettings")]
        public Task<int> CreateFromSettings([FromQuery] string name)
        {
            return Task.FromResult(SinkManager.Instance.AddNetworkTablesSinkFromSettings(name));
        }

        // GET: NT4 connection status for a given sink
        [HttpGet("networkTablesSink/status")]
        public Task<NetworkTablesStatusDto> GetStatus([FromQuery] int sinkId)
        {
            return Task.FromResult(NetworkTablesStatusDto.Parse(SinkManager.Instance.GetNetworkTablesSinkStatus(sinkId)));
        }
    }
}
