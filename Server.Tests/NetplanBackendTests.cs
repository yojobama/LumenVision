using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Server;
using Server.Web;
using Xunit;

namespace Server.Tests;

public class NetplanBackendTests : IDisposable
{
    private readonly string staging = Path.Combine(Path.GetTempPath(), $"lumen-netplan-{Guid.NewGuid():N}");
    private readonly FakeRunner runner = new();
    private string? installedContent;

    public NetplanBackendTests()
    {
        Directory.CreateDirectory(staging);
        runner.Respond = (program, args) =>
        {
            if (program == "netplan" && args[0] == "get") return new CommandResult(0, "network:\n", "");
            if (program == "ip" && args.Contains("link")) return new CommandResult(0,
                "1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536\n2: enP3p49s0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500\n3: enP4p65s0: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500\n4: docker0: <NO-CARRIER> mtu 1500\n5: veth12@if3: <BROADCAST> mtu 1500\n", "");
            if (program == "ip" && args.Contains("addr")) return new CommandResult(0, "2: enP3p49s0    inet 192.168.68.123/24 metric 100 brd 192.168.68.255 scope global dynamic enP3p49s0\n", "");
            if (program == "install")
            {
                installedContent = File.ReadAllText(args[^2]); // the staged file is deleted afterwards
                return new CommandResult(0, "", "");
            }
            if (program == "cat") return new CommandResult(1, "", "No such file");
            return new CommandResult(0, "", "");
        };
    }

    public void Dispose() => Directory.Delete(staging, true);

    private NetplanNetworkBackend Backend() => new(() => runner, staging);

    private static Ipv4Config Static() => new("static", "10.12.34.11/24", "10.12.34.1", new[] { "8.8.8.8", "1.1.1.1" });

    [Fact]
    public async Task OnlyPhysicalEthernetDevicesAreListedWithTheirCurrentAddresses()
    {
        var connections = await Backend().ListAsync();

        Assert.Equal(new[] { "enP3p49s0", "enP4p65s0" }, connections.Select(c => c.Name).ToArray());
        Assert.All(connections, c => Assert.Equal("dhcp", c.Configured.Method));
        Assert.Equal(new[] { "192.168.68.123/24" }, connections[0].CurrentAddresses);
    }

    [Fact]
    public async Task NetplanIsAvailableWhenItsConfigurationReads()
    {
        Assert.True(await Backend().IsAvailableAsync());
        runner.Respond = (_, _) => new CommandResult(1, "", "netplan: command not found");
        Assert.False(await Backend().IsAvailableAsync());
    }

    [Fact]
    public void AStaticSetupRendersAndReadsBackIdentically()
    {
        string yaml = NetplanNetworkBackend.Render("enP3p49s0", Static());

        Assert.Contains("aa-lumenvision-enP3p49s0:", yaml);
        Assert.Contains("name: \"enP3p49s0\"", yaml);
        Assert.Contains("dhcp4: false", yaml);
        Assert.Contains("- 10.12.34.11/24", yaml);
        Assert.Contains("via: 10.12.34.1", yaml);
        Assert.Contains("addresses: [8.8.8.8, 1.1.1.1]", yaml);
        Assert.Equal(Static().Address, NetplanNetworkBackend.Parse(yaml).Address);
        Assert.Equal(Static().Gateway, NetplanNetworkBackend.Parse(yaml).Gateway);
        Assert.Equal(Static().Dns, NetplanNetworkBackend.Parse(yaml).Dns);
        Assert.Equal("static", NetplanNetworkBackend.Parse(yaml).Method);
    }

    [Fact]
    public void WithoutAGatewayOrDnsNeitherIsWritten()
    {
        string yaml = NetplanNetworkBackend.Render("eth0", new Ipv4Config("static", "192.168.1.5/24", null, Array.Empty<string>()));

        Assert.DoesNotContain("routes:", yaml);
        Assert.DoesNotContain("nameservers:", yaml);
        var parsed = NetplanNetworkBackend.Parse(yaml);
        Assert.Null(parsed.Gateway);
        Assert.Empty(parsed.Dns);
    }

    [Fact]
    public void ADifferentFileCountsAsDhcp()
    {
        Assert.Equal("dhcp", NetplanNetworkBackend.Parse("network:\n  version: 2\n").Method);
    }

    [Fact]
    public async Task AStaticWriteInstallsARootOnlyFileThenChecksItWithNetplanGenerate()
    {
        await Backend().WriteAsync("enP3p49s0", Static());

        var install = runner.Calls.Single(c => c.Program == "install");
        Assert.Equal(new[] { "-m", "600", "-o", "root", "-g", "root" }, install.Arguments.Take(6).ToArray());
        Assert.Equal("/etc/netplan/60-lumenvision-enP3p49s0.yaml", install.Arguments[^1]);
        Assert.Contains("- 10.12.34.11/24", installedContent);
        Assert.Contains(runner.Calls, c => c.Program == "netplan" && c.Arguments.SequenceEqual(new[] { "generate" }));
        Assert.Empty(Directory.GetFiles(staging)); // the staged copy is gone
    }

    [Fact]
    public async Task APlanNetplanRefusesIsRemovedAgainAndReported()
    {
        var inner = runner.Respond;
        runner.Respond = (program, args) => program == "netplan" && args[0] == "generate" ? new CommandResult(1, "", "bad yaml") : inner(program, args);

        var ex = await Assert.ThrowsAsync<ApiException>(() => Backend().WriteAsync("enP3p49s0", Static()));

        Assert.Contains("bad yaml", ex.Message);
        Assert.Contains(runner.Calls, c => c.Program == "rm" && c.Arguments.SequenceEqual(new[] { "-f", "/etc/netplan/60-lumenvision-enP3p49s0.yaml" }));
    }

    [Fact]
    public async Task GoingBackToDhcpRemovesOurFile()
    {
        await Backend().WriteAsync("enP3p49s0", new Ipv4Config("dhcp", null, null, Array.Empty<string>()));

        Assert.Contains(runner.Calls, c => c.Program == "rm" && c.Arguments.SequenceEqual(new[] { "-f", "/etc/netplan/60-lumenvision-enP3p49s0.yaml" }));
        Assert.DoesNotContain(runner.Calls, c => c.Program == "install");
    }

    [Theory]
    [InlineData("lo")]
    [InlineData("docker0")]
    [InlineData("../etc/passwd")]
    [InlineData("eth0; reboot")]
    [InlineData("enP3p49s0\"\n  evil")]
    public async Task OnlyEthernetInterfaceNamesAreAccepted(string name)
    {
        var ex = await Assert.ThrowsAsync<ApiException>(() => Backend().WriteAsync(name, Static()));
        Assert.Equal(400, ex.StatusCode);
        Assert.Empty(runner.Calls);
    }

    [Fact]
    public async Task ActivatingRunsNetplanApply()
    {
        await Backend().ActivateAsync("enP3p49s0");

        Assert.Contains(runner.Calls, c => c.Program == "netplan" && c.Arguments.SequenceEqual(new[] { "apply" }));
    }

    [Fact]
    public async Task TheServiceUsesNetplanWhereNetworkManagerIsAbsent()
    {
        runner.Respond = (program, args) =>
        {
            if (program == "nmcli") return new CommandResult(1, "", "not running");
            if (program == "netplan" && args[0] == "get") return new CommandResult(0, "", "");
            if (program == "ip" && args.Contains("link")) return new CommandResult(0, "2: enP3p49s0: <UP>\n", "");
            return new CommandResult(1, "", "");
        };
        var service = new NetworkService(runner, TimeSpan.FromMinutes(1), Path.Combine(staging, "pending.json"));

        NetworkStatus status = await service.GetStatusAsync();

        Assert.True(status.Supported);
        Assert.Equal("netplan", status.Mechanism);
        Assert.Equal("enP3p49s0", Assert.Single(status.Connections).Name);
    }
}
