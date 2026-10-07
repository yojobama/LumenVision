using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Server.Web;

namespace Server
{
    // One IPv4 setup of a NetworkManager connection. Method is "dhcp" or "static".
    public sealed record Ipv4Config(string Method, string? Address, string? Gateway, string[] Dns);

    public sealed record NetworkConnectionInfo(string Name, string Device, string Type, Ipv4Config Configured, string[] CurrentAddresses);

    public sealed record PendingNetworkChange(string Connection, int SecondsLeft);

    // Mechanism: "NetworkManager" or "netplan", or null when the address cannot be changed from here
    public sealed record NetworkStatus(bool Supported, string Hostname, NetworkConnectionInfo[] Connections, PendingNetworkChange? Pending, string? Mechanism = null);

    // Hostname and IPv4 configuration through NetworkManager (nmcli). A static address that does not work would cut the device off, so a change
    // starts a countdown and is undone unless the user confirms it from the new address; an unconfirmed change is also undone if the server
    // restarts before the confirmation (the pending state is kept in a file for that).
    public sealed class NetworkService
    {
        public static NetworkService Instance { get; } = new NetworkService(() => PrivilegedCommand.Runner, TimeSpan.FromSeconds(60), "network-pending.json");

        private readonly Func<IPrivilegedRunner> runner;
        private readonly Func<IEnumerable<INetworkBackend>> backends;
        private readonly TimeSpan revertAfter;
        private readonly string pendingFile;
        private readonly object sync = new object();
        private CancellationTokenSource? countdown;
        private DateTime? revertAt;
        private string? pendingConnection;

        public NetworkService(IPrivilegedRunner runner, TimeSpan revertAfter, string pendingFile)
            : this(() => runner, revertAfter, pendingFile) { }

        // the runner is read on every call so tests can swap PrivilegedCommand.Runner after the singleton exists
        public NetworkService(Func<IPrivilegedRunner> runner, TimeSpan revertAfter, string pendingFile)
            : this(runner, revertAfter, pendingFile, () => new INetworkBackend[] { new NmcliNetworkBackend(runner), new NetplanNetworkBackend(runner) }) { }

        public NetworkService(Func<IPrivilegedRunner> runner, TimeSpan revertAfter, string pendingFile, Func<IEnumerable<INetworkBackend>> backends)
        {
            this.runner = runner;
            this.revertAfter = revertAfter;
            this.pendingFile = pendingFile;
            this.backends = backends;
        }

        // the first mechanism this device actually uses, or null
        private async Task<INetworkBackend?> BackendAsync()
        {
            foreach (INetworkBackend backend in backends())
            {
                if (await backend.IsAvailableAsync()) return backend;
            }
            return null;
        }

        // ---- reading ----

        public async Task<NetworkStatus> GetStatusAsync()
        {
            string hostname = await ReadHostnameAsync();
            INetworkBackend? backend = await BackendAsync();
            if (backend == null) return new NetworkStatus(false, hostname, Array.Empty<NetworkConnectionInfo>(), CurrentPending());
            return new NetworkStatus(true, hostname, await backend.ListAsync(), CurrentPending(), backend.Name);
        }

        public async Task<bool> IsAvailableAsync() => await BackendAsync() != null;

        private async Task<string> ReadHostnameAsync()
        {
            try
            {
                return File.Exists("/etc/hostname") ? (await File.ReadAllTextAsync("/etc/hostname")).Trim() : Dns.GetHostName();
            }
            catch (IOException)
            {
                return Dns.GetHostName();
            }
        }

        // ---- hostname ----

        private static readonly Regex HostnamePattern = new Regex("^[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?$", RegexOptions.Compiled);

        public static string? ValidateHostname(string? name) =>
            name != null && HostnamePattern.IsMatch(name) ? null : "a hostname is 1 to 63 lower-case letters, digits or '-', and cannot start or end with '-'";

        public async Task SetHostnameAsync(string name)
        {
            string? problem = ValidateHostname(name);
            if (problem != null) throw ApiException.BadRequest(problem);

            CommandResult result = await runner().RunAsync("hostnamectl", new[] { "set-hostname", name });
            if (!result.Ok) throw new InvalidOperationException($"hostnamectl failed: {result.StdErr.Trim()}");
            // the mDNS name (name.local) follows the hostname once avahi restarts; a board without avahi simply has nothing to restart
            await runner().RunAsync("systemctl", new[] { "restart", "avahi-daemon" });
        }

        // ---- IPv4 ----

        // The problem with a requested IPv4 setup, or null when it is usable.
        public static string? ValidateIpv4(Ipv4Config config)
        {
            if (config.Method == "dhcp") return null;
            if (config.Method != "static") return "the method must be 'dhcp' or 'static'";
            if (string.IsNullOrWhiteSpace(config.Address)) return "a static setup needs an address such as 10.12.34.11/24";

            string[] parts = config.Address.Split('/');
            if (parts.Length != 2 || !IPAddress.TryParse(parts[0], out IPAddress? address) || address.AddressFamily != AddressFamily.InterNetwork
                || !int.TryParse(parts[1], out int prefix) || prefix < 1 || prefix > 30)
                return "the address must be an IPv4 address with a prefix length from 1 to 30, such as 10.12.34.11/24";
            if (address.Equals(IPAddress.Any) || address.Equals(IPAddress.Broadcast) || address.GetAddressBytes()[0] is 127 or >= 224)
                return "that address cannot be used by a device";

            if (!string.IsNullOrWhiteSpace(config.Gateway))
            {
                if (!IPAddress.TryParse(config.Gateway, out IPAddress? gateway) || gateway.AddressFamily != AddressFamily.InterNetwork)
                    return "the gateway must be an IPv4 address";
                if (!SameSubnet(address, gateway, prefix)) return "the gateway must be inside the address's subnet";
            }
            foreach (string dns in config.Dns)
            {
                if (!IPAddress.TryParse(dns, out IPAddress? server) || server.AddressFamily != AddressFamily.InterNetwork) return $"'{dns}' is not an IPv4 DNS server";
            }
            return null;
        }

        private static bool SameSubnet(IPAddress a, IPAddress b, int prefix)
        {
            uint ToUInt(IPAddress ip) { byte[] x = ip.GetAddressBytes(); return (uint)(x[0] << 24 | x[1] << 16 | x[2] << 8 | x[3]); }
            uint mask = prefix == 0 ? 0 : uint.MaxValue << (32 - prefix);
            return (ToUInt(a) & mask) == (ToUInt(b) & mask);
        }

        // Applies a new IPv4 setup to a connection and starts the revert countdown. The old setup is restored after `revertAfter` unless
        // ConfirmAsync is called first. The connection is brought up shortly after this returns, so the caller's session may drop.
        public async Task<PendingNetworkChange> SetIpv4Async(string connection, Ipv4Config requested)
        {
            string? problem = ValidateIpv4(requested);
            if (problem != null) throw ApiException.BadRequest(problem);
            INetworkBackend backend = await BackendAsync()
                ?? throw ApiException.BadRequest("this device is managed by neither NetworkManager nor netplan; set the address with the operating system's own tools");
            if (CurrentPending() != null) throw ApiException.BadRequest("an earlier network change is still waiting to be confirmed");

            Ipv4Config previous = await backend.ReadAsync(connection);
            WritePending(connection, previous);

            try
            {
                await backend.WriteAsync(connection, requested);
            }
            catch
            {
                ClearPending();
                throw;
            }

            StartCountdown(connection, previous);
            // bring it up after the response is on its way
            _ = Task.Run(async () =>
            {
                await Task.Delay(TimeSpan.FromSeconds(1));
                await backend.ActivateAsync(connection);
            });
            return CurrentPending()!;
        }

        // keeps the new setup
        public void Confirm()
        {
            lock (sync)
            {
                countdown?.Cancel();
                countdown = null;
                revertAt = null;
                pendingConnection = null;
            }
            ClearPending();
        }

        // puts the previous setup back now
        public async Task RevertNowAsync()
        {
            Pending? saved = ReadPending();
            if (saved == null) return;
            lock (sync)
            {
                countdown?.Cancel();
                countdown = null;
                revertAt = null;
                pendingConnection = null;
            }
            await RestoreAsync(saved.Connection, saved.Previous);
        }

        // At server start: a change that was never confirmed (the server restarted first) is undone.
        public async Task RecoverAsync()
        {
            Pending? saved = ReadPending();
            if (saved == null) return;
            Console.WriteLine($"[network] an unconfirmed change to '{saved.Connection}' was found at start-up; restoring the previous setup");
            await RestoreAsync(saved.Connection, saved.Previous);
        }

        private async Task RestoreAsync(string connection, Ipv4Config previous)
        {
            try
            {
                INetworkBackend? backend = await BackendAsync();
                if (backend == null) return;
                await backend.WriteAsync(connection, previous);
                await backend.ActivateAsync(connection);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[network] restoring '{connection}' failed: {ex.Message}");
            }
            finally
            {
                ClearPending();
            }
        }

        private void StartCountdown(string connection, Ipv4Config previous)
        {
            lock (sync)
            {
                countdown?.Cancel();
                countdown = new CancellationTokenSource();
                revertAt = DateTime.UtcNow + revertAfter;
                pendingConnection = connection;
                CancellationToken token = countdown.Token;
                _ = Task.Run(async () =>
                {
                    try
                    {
                        await Task.Delay(revertAfter, token);
                    }
                    catch (OperationCanceledException)
                    {
                        return;
                    }
                    lock (sync)
                    {
                        if (token.IsCancellationRequested) return;
                        revertAt = null;
                        pendingConnection = null;
                    }
                    Console.WriteLine($"[network] change to '{connection}' was not confirmed in time; restoring the previous setup");
                    await RestoreAsync(connection, previous);
                });
            }
        }

        public PendingNetworkChange? CurrentPending()
        {
            lock (sync)
            {
                if (revertAt == null || pendingConnection == null) return null;
                return new PendingNetworkChange(pendingConnection, Math.Max(0, (int)Math.Ceiling((revertAt.Value - DateTime.UtcNow).TotalSeconds)));
            }
        }

        // ---- the pending file ----

        private sealed record Pending(string Connection, Ipv4Config Previous);

        private void WritePending(string connection, Ipv4Config previous) =>
            File.WriteAllText(pendingFile, JsonSerializer.Serialize(new Pending(connection, previous)));

        private Pending? ReadPending()
        {
            try
            {
                return File.Exists(pendingFile) ? JsonSerializer.Deserialize<Pending>(File.ReadAllText(pendingFile)) : null;
            }
            catch (Exception ex) when (ex is IOException or JsonException)
            {
                return null;
            }
        }

        private void ClearPending()
        {
            try { if (File.Exists(pendingFile)) File.Delete(pendingFile); } catch (IOException) { /* a stale file only causes one extra restore at the next start */ }
        }

        public static string[] SplitTerse(string line) => NetworkParsing.SplitTerse(line);
    }
}
