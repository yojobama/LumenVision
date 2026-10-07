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

    public sealed record NetworkStatus(bool Supported, string Hostname, NetworkConnectionInfo[] Connections, PendingNetworkChange? Pending);

    // Hostname and IPv4 configuration through NetworkManager (nmcli). A static address that does not work would cut the device off, so a change
    // starts a countdown and is undone unless the user confirms it from the new address; an unconfirmed change is also undone if the server
    // restarts before the confirmation (the pending state is kept in a file for that).
    public sealed class NetworkService
    {
        public static NetworkService Instance { get; } = new NetworkService(() => PrivilegedCommand.Runner, TimeSpan.FromSeconds(60), "network-pending.json");

        private readonly Func<IPrivilegedRunner> runner;
        private readonly TimeSpan revertAfter;
        private readonly string pendingFile;
        private readonly object sync = new object();
        private CancellationTokenSource? countdown;
        private DateTime? revertAt;
        private string? pendingConnection;

        // the runner is read on every call so tests can swap PrivilegedCommand.Runner after the singleton exists
        public NetworkService(IPrivilegedRunner runner, TimeSpan revertAfter, string pendingFile)
            : this(() => runner, revertAfter, pendingFile) { }

        public NetworkService(Func<IPrivilegedRunner> runner, TimeSpan revertAfter, string pendingFile)
        {
            this.runner = runner;
            this.revertAfter = revertAfter;
            this.pendingFile = pendingFile;
        }

        private Task<CommandResult> Run(params string[] arguments) => runner().RunAsync("nmcli", arguments);

        // ---- reading ----

        public async Task<NetworkStatus> GetStatusAsync()
        {
            string hostname = await ReadHostnameAsync();
            if (!await IsAvailableAsync()) return new NetworkStatus(false, hostname, Array.Empty<NetworkConnectionInfo>(), CurrentPending());

            var connections = new List<NetworkConnectionInfo>();
            CommandResult active = await Run("-t", "-f", "NAME,TYPE,DEVICE", "connection", "show", "--active");
            foreach (string line in Lines(active.StdOut))
            {
                string[] fields = SplitTerse(line);
                if (fields.Length < 3 || fields[1] != "802-3-ethernet" && fields[1] != "802-11-wireless") continue;
                connections.Add(new NetworkConnectionInfo(fields[0], fields[2], fields[1], await ReadIpv4Async(fields[0]), await ReadCurrentAddressesAsync(fields[2])));
            }
            return new NetworkStatus(true, hostname, connections.ToArray(), CurrentPending());
        }

        public async Task<bool> IsAvailableAsync()
        {
            try
            {
                return (await Run("-t", "general", "status")).Ok;
            }
            catch (Exception ex) when (ex is PlatformNotSupportedException or System.ComponentModel.Win32Exception)
            {
                return false;
            }
        }

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

        private async Task<Ipv4Config> ReadIpv4Async(string connection)
        {
            CommandResult details = await Run("-t", "-f", "ipv4.method,ipv4.addresses,ipv4.gateway,ipv4.dns", "connection", "show", connection);
            var values = new Dictionary<string, string>();
            foreach (string line in Lines(details.StdOut))
            {
                int colon = line.IndexOf(':');
                if (colon > 0) values[line[..colon]] = line[(colon + 1)..].Replace("\\:", ":");
            }
            string Get(string key) => values.TryGetValue(key, out string? v) ? v.Trim() : "";
            string[] SplitList(string text) => text.Split(new[] { ',', ';' }, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries).Where(s => s != "--").ToArray();

            string[] addresses = SplitList(Get("ipv4.addresses"));
            string gateway = Get("ipv4.gateway");
            return new Ipv4Config(Get("ipv4.method") == "manual" ? "static" : "dhcp", addresses.FirstOrDefault(),
                gateway == "" || gateway == "--" ? null : gateway, SplitList(Get("ipv4.dns")));
        }

        private async Task<string[]> ReadCurrentAddressesAsync(string device)
        {
            CommandResult result = await Run("-t", "-f", "IP4.ADDRESS", "device", "show", device);
            return Lines(result.StdOut).Select(l => l[(l.IndexOf(':') + 1)..].Trim()).Where(s => s.Length > 0).ToArray();
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

        private static string[] ModifyArguments(string connection, Ipv4Config config)
        {
            bool manual = config.Method == "static";
            return new[]
            {
                "connection", "modify", connection,
                "ipv4.method", manual ? "manual" : "auto",
                "ipv4.addresses", manual ? config.Address ?? "" : "",
                "ipv4.gateway", manual ? config.Gateway ?? "" : "",
                "ipv4.dns", string.Join(",", config.Dns),
            };
        }

        // Applies a new IPv4 setup to a connection and starts the revert countdown. The old setup is restored after `revertAfter` unless
        // ConfirmAsync is called first. The connection is brought up shortly after this returns, so the caller's session may drop.
        public async Task<PendingNetworkChange> SetIpv4Async(string connection, Ipv4Config requested)
        {
            string? problem = ValidateIpv4(requested);
            if (problem != null) throw ApiException.BadRequest(problem);
            if (!await IsAvailableAsync()) throw ApiException.BadRequest("this device is not managed by NetworkManager; set the address with the operating system's own tools");
            if (CurrentPending() != null) throw ApiException.BadRequest("an earlier network change is still waiting to be confirmed");

            Ipv4Config previous = await ReadIpv4Async(connection);
            WritePending(connection, previous);

            CommandResult modified = await Run(ModifyArguments(connection, requested));
            if (!modified.Ok)
            {
                ClearPending();
                throw ApiException.BadRequest($"NetworkManager rejected the change: {modified.StdErr.Trim()}");
            }

            StartCountdown(connection, previous);
            // bring it up after the response is on its way
            _ = Task.Run(async () =>
            {
                await Task.Delay(TimeSpan.FromSeconds(1));
                CommandResult up = await Run("connection", "up", connection);
                if (!up.Ok) Console.WriteLine($"[network] connection up failed: {up.StdErr.Trim()}");
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
                CommandResult modified = await Run(ModifyArguments(connection, previous));
                if (modified.Ok) await Run("connection", "up", connection);
                else Console.WriteLine($"[network] restoring '{connection}' failed: {modified.StdErr.Trim()}");
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

        // ---- nmcli's terse output ----

        private static IEnumerable<string> Lines(string text) => text.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

        // fields are separated by ':' and a literal ':' (or '\') inside one is escaped with a backslash
        public static string[] SplitTerse(string line)
        {
            var fields = new List<string>();
            var current = new System.Text.StringBuilder();
            for (int i = 0; i < line.Length; i++)
            {
                if (line[i] == '\\' && i + 1 < line.Length) current.Append(line[++i]);
                else if (line[i] == ':') { fields.Add(current.ToString()); current.Clear(); }
                else current.Append(line[i]);
            }
            fields.Add(current.ToString());
            return fields.ToArray();
        }
    }
}
