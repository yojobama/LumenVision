using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers
{
    // The device's hostname and IPv4 setup (NetworkManager). Changing either runs privileged commands (see PrivilegedCommand).
    internal class NetworkController : ControllerBase
    {
        public record HostnameRequest(string Name);
        public record Ipv4Request(string Connection, string Method, string? Address, string? Gateway, string[]? Dns);

        // GET: the hostname, each active ethernet/wifi connection with its configured and current addresses, and any change waiting to be confirmed
        [HttpGet("network")]
        public Task<NetworkStatus> Get()
        {
            return NetworkService.Instance.GetStatusAsync();
        }

        // PUT: rename the device (lower-case letters, digits and '-'); the mDNS name follows
        [HttpPut("network/hostname")]
        public async Task SetHostname()
        {
            var request = await ReadBody<HostnameRequest>();
            await NetworkService.Instance.SetHostnameAsync(request.Name);
        }

        // PUT: set a connection to DHCP or a static address. The change is undone after a minute unless POST network/confirm arrives (from the new
        // address, which proves it works); the connection is brought up a second after this returns, so the caller's session may drop.
        [HttpPut("network/ipv4")]
        public async Task<PendingNetworkChange> SetIpv4()
        {
            var request = await ReadBody<Ipv4Request>();
            return await NetworkService.Instance.SetIpv4Async(request.Connection,
                new Ipv4Config(request.Method, request.Address, request.Gateway, request.Dns ?? System.Array.Empty<string>()));
        }

        // POST: keep the network change that is waiting
        [HttpPost("network/confirm")]
        public Task Confirm()
        {
            NetworkService.Instance.Confirm();
            return Task.CompletedTask;
        }

        // POST: undo the network change that is waiting, now
        [HttpPost("network/revert")]
        public Task Revert()
        {
            return NetworkService.Instance.RevertNowAsync();
        }

        private async Task<T> ReadBody<T>()
        {
            string body = await HttpContext.GetRequestBodyAsStringAsync();
            try
            {
                return System.Text.Json.JsonSerializer.Deserialize<T>(body) ?? throw ApiException.BadRequest("the body is empty");
            }
            catch (System.Text.Json.JsonException)
            {
                throw ApiException.BadRequest("the body is not valid JSON for this request");
            }
        }
    }
}
