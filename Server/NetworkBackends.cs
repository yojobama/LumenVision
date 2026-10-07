using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Server.Web;

namespace Server
{
    // How the device's IPv4 setup is read and changed: NetworkManager (nmcli) or netplan/systemd-networkd, which is what the board images use.
    public interface INetworkBackend
    {
        // what the UI calls the mechanism, for messages
        string Name { get; }
        Task<bool> IsAvailableAsync();
        Task<NetworkConnectionInfo[]> ListAsync();
        Task<Ipv4Config> ReadAsync(string connection);
        // Stores the setup without activating it; throws ApiException when the system rejects it.
        Task WriteAsync(string connection, Ipv4Config config);
        // Makes the stored setup live. May drop the caller's network session.
        Task ActivateAsync(string connection);
    }

    public sealed class NmcliNetworkBackend : INetworkBackend
    {
        private readonly Func<IPrivilegedRunner> runner;

        public NmcliNetworkBackend(Func<IPrivilegedRunner> runner) => this.runner = runner;

        public string Name => "NetworkManager";

        private Task<CommandResult> Run(params string[] arguments) => runner().RunAsync("nmcli", arguments);

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

        public async Task<NetworkConnectionInfo[]> ListAsync()
        {
            var connections = new List<NetworkConnectionInfo>();
            CommandResult active = await Run("-t", "-f", "NAME,TYPE,DEVICE", "connection", "show", "--active");
            foreach (string line in NetworkParsing.Lines(active.StdOut))
            {
                string[] fields = NetworkParsing.SplitTerse(line);
                if (fields.Length < 3 || fields[1] != "802-3-ethernet" && fields[1] != "802-11-wireless") continue;
                connections.Add(new NetworkConnectionInfo(fields[0], fields[2], fields[1], await ReadAsync(fields[0]), await ReadCurrentAddressesAsync(fields[2])));
            }
            return connections.ToArray();
        }

        public async Task<Ipv4Config> ReadAsync(string connection)
        {
            CommandResult details = await Run("-t", "-f", "ipv4.method,ipv4.addresses,ipv4.gateway,ipv4.dns", "connection", "show", connection);
            var values = new Dictionary<string, string>();
            foreach (string line in NetworkParsing.Lines(details.StdOut))
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
            return NetworkParsing.Lines(result.StdOut).Select(l => l[(l.IndexOf(':') + 1)..].Trim()).Where(s => s.Length > 0).ToArray();
        }

        public async Task WriteAsync(string connection, Ipv4Config config)
        {
            bool manual = config.Method == "static";
            CommandResult modified = await Run("connection", "modify", connection,
                "ipv4.method", manual ? "manual" : "auto",
                "ipv4.addresses", manual ? config.Address ?? "" : "",
                "ipv4.gateway", manual ? config.Gateway ?? "" : "",
                "ipv4.dns", string.Join(",", config.Dns));
            if (!modified.Ok) throw ApiException.BadRequest($"NetworkManager rejected the change: {modified.StdErr.Trim()}");
        }

        public async Task ActivateAsync(string connection)
        {
            CommandResult up = await Run("connection", "up", connection);
            if (!up.Ok) Console.WriteLine($"[network] connection up failed: {up.StdErr.Trim()}");
        }
    }

    // netplan with the networkd renderer (the Armbian images). A static setup is one file per interface, /etc/netplan/60-lumenvision-<interface>.yaml;
    // DHCP is the image's own catch-all configuration, so going back to DHCP just removes that file. The file's netplan id starts with "aa-" so
    // networkd, which uses the first matching .network file in name order, picks it before the image's "all-eth-interfaces".
    public sealed class NetplanNetworkBackend : INetworkBackend
    {
        public const string ConfigDirectory = "/etc/netplan";
        private static readonly Regex InterfacePattern = new Regex("^[A-Za-z0-9_.-]{1,15}$", RegexOptions.Compiled);
        // physical network devices: the kernel's predictable names (enp3s0, enP3p49s0), eth0 and the lanX/wanX names some boards use
        private static readonly Regex EthernetName = new Regex("^(en|eth|lan|wan)[A-Za-z0-9_.-]*$", RegexOptions.Compiled);

        private readonly Func<IPrivilegedRunner> runner;
        private readonly string stagingDirectory;

        public NetplanNetworkBackend(Func<IPrivilegedRunner> runner, string? stagingDirectory = null)
        {
            this.runner = runner;
            this.stagingDirectory = stagingDirectory ?? Path.GetTempPath();
        }

        public string Name => "netplan";

        public static string FilePath(string device) => $"{ConfigDirectory}/60-lumenvision-{device}.yaml";

        private Task<CommandResult> Run(string program, params string[] arguments) => runner().RunAsync(program, arguments);

        public async Task<bool> IsAvailableAsync()
        {
            try
            {
                // present, and the directory holds a configuration netplan accepts
                return (await Run("netplan", "get", "all")).Ok;
            }
            catch (Exception ex) when (ex is PlatformNotSupportedException or System.ComponentModel.Win32Exception)
            {
                return false;
            }
        }

        public async Task<NetworkConnectionInfo[]> ListAsync()
        {
            CommandResult links = await Run("ip", "-o", "link", "show");
            var connections = new List<NetworkConnectionInfo>();
            foreach (string device in ParseEthernetDevices(links.StdOut))
            {
                CommandResult addresses = await Run("ip", "-o", "-4", "addr", "show", "dev", device);
                connections.Add(new NetworkConnectionInfo(device, device, "802-3-ethernet", await ReadAsync(device), ParseAddresses(addresses.StdOut)));
            }
            return connections.ToArray();
        }

        // "2: enP3p49s0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 ..." -> enP3p49s0; virtual devices (names with @, docker, veth...) do not match
        public static string[] ParseEthernetDevices(string output) =>
            NetworkParsing.Lines(output)
                .Select(line => line.Split(':', 3))
                .Where(parts => parts.Length >= 2)
                .Select(parts => parts[1].Trim())
                .Where(name => EthernetName.IsMatch(name))
                .ToArray();

        // "2: enP3p49s0    inet 192.168.68.123/24 metric 100 brd ..." -> 192.168.68.123/24
        public static string[] ParseAddresses(string output) =>
            NetworkParsing.Lines(output)
                .Select(line => Regex.Match(line, @"\binet\s+(\d+\.\d+\.\d+\.\d+/\d+)"))
                .Where(m => m.Success)
                .Select(m => m.Groups[1].Value)
                .ToArray();

        // ---- our file ----

        public static string Render(string device, Ipv4Config config)
        {
            var yaml = new StringBuilder();
            yaml.AppendLine("# Written by LumenVision's device settings; delete this file (or switch the interface back to DHCP there) to return to the default.");
            yaml.AppendLine("network:");
            yaml.AppendLine("  version: 2");
            yaml.AppendLine("  renderer: networkd");
            yaml.AppendLine("  ethernets:");
            yaml.AppendLine($"    aa-lumenvision-{device}:");
            yaml.AppendLine("      match:");
            yaml.AppendLine($"        name: \"{device}\"");
            yaml.AppendLine("      dhcp4: false");
            yaml.AppendLine("      addresses:");
            yaml.AppendLine($"        - {config.Address}");
            if (!string.IsNullOrWhiteSpace(config.Gateway))
            {
                yaml.AppendLine("      routes:");
                yaml.AppendLine("        - to: default");
                yaml.AppendLine($"          via: {config.Gateway}");
            }
            if (config.Dns.Length > 0)
            {
                yaml.AppendLine("      nameservers:");
                yaml.AppendLine($"        addresses: [{string.Join(", ", config.Dns)}]");
            }
            return yaml.ToString();
        }

        // reads back a file Render wrote; anything else (no file, a file someone edited) counts as DHCP
        public static Ipv4Config Parse(string yaml)
        {
            Match address = Regex.Match(yaml, @"^\s+addresses:\s*\r?\n\s+-\s+(\d+\.\d+\.\d+\.\d+/\d+)", RegexOptions.Multiline);
            if (!address.Success) return new Ipv4Config("dhcp", null, null, Array.Empty<string>());
            Match gateway = Regex.Match(yaml, @"via:\s*(\d+\.\d+\.\d+\.\d+)");
            Match dns = Regex.Match(yaml, @"nameservers:\s*\r?\n\s+addresses:\s*\[([^\]]*)\]");
            string[] servers = dns.Success ? dns.Groups[1].Value.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries) : Array.Empty<string>();
            return new Ipv4Config("static", address.Groups[1].Value, gateway.Success ? gateway.Groups[1].Value : null, servers);
        }

        public async Task<Ipv4Config> ReadAsync(string connection)
        {
            RequireDevice(connection);
            CommandResult text = await Run("cat", FilePath(connection));
            return text.Ok ? Parse(text.StdOut) : new Ipv4Config("dhcp", null, null, Array.Empty<string>());
        }

        private static void RequireDevice(string connection)
        {
            if (!InterfacePattern.IsMatch(connection) || !EthernetName.IsMatch(connection)) throw ApiException.BadRequest($"'{connection}' is not a network interface this page can configure");
        }

        public async Task WriteAsync(string connection, Ipv4Config config)
        {
            RequireDevice(connection);
            string target = FilePath(connection);
            if (config.Method != "static")
            {
                await Run("rm", "-f", target);
                return;
            }

            // staged in a file the server owns, then installed root-only (netplan warns about configuration files others can read)
            string staged = Path.Combine(stagingDirectory, $"lumenvision-netplan-{Guid.NewGuid():N}.yaml");
            await File.WriteAllTextAsync(staged, Render(connection, config));
            try
            {
                CommandResult installed = await Run("install", "-m", "600", "-o", "root", "-g", "root", staged, target);
                if (!installed.Ok) throw new InvalidOperationException($"could not write {target}: {installed.StdErr.Trim()}");
            }
            finally
            {
                File.Delete(staged);
            }

            CommandResult generated = await Run("netplan", "generate");
            if (!generated.Ok)
            {
                await Run("rm", "-f", target);
                throw ApiException.BadRequest($"netplan rejected the change: {generated.StdErr.Trim()}");
            }
        }

        public async Task ActivateAsync(string connection)
        {
            CommandResult applied = await Run("netplan", "apply");
            if (!applied.Ok) Console.WriteLine($"[network] netplan apply failed: {applied.StdErr.Trim()}");
        }
    }

    public static class NetworkParsing
    {
        public static IEnumerable<string> Lines(string text) => text.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

        // fields are separated by ':' and a literal ':' (or '\') inside one is escaped with a backslash (nmcli's terse output)
        public static string[] SplitTerse(string line)
        {
            var fields = new List<string>();
            var current = new StringBuilder();
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
