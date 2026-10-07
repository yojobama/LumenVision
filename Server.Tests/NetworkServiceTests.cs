using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Server;
using Server.Web;
using Xunit;

namespace Server.Tests;

public class NetworkServiceTests : IDisposable
{
    private readonly string pendingFile = Path.Combine(Path.GetTempPath(), $"lumen-network-{Guid.NewGuid():N}.json");
    private readonly FakeRunner runner = new();

    public NetworkServiceTests()
    {
        runner.Respond = (program, args) =>
        {
            string joined = string.Join(' ', args);
            if (program == "nmcli" && joined.StartsWith("-t general status")) return new CommandResult(0, "connected:full:enabled\n", "");
            if (program == "nmcli" && joined.Contains("NAME,TYPE,DEVICE")) return new CommandResult(0, "Wired connection 1:802-3-ethernet:end0\nlo:loopback:lo\n", "");
            if (program == "nmcli" && joined.Contains("ipv4.method,ipv4.addresses")) return new CommandResult(0, "ipv4.method:auto\nipv4.addresses:\nipv4.gateway:\nipv4.dns:\n", "");
            if (program == "nmcli" && joined.Contains("IP4.ADDRESS")) return new CommandResult(0, "IP4.ADDRESS[1]:192.168.55.40/24\n", "");
            return new CommandResult(0, "", "");
        };
    }

    public void Dispose()
    {
        if (File.Exists(pendingFile)) File.Delete(pendingFile);
    }

    private NetworkService Service(TimeSpan? revertAfter = null) => new(runner, revertAfter ?? TimeSpan.FromMinutes(5), pendingFile);

    private static Ipv4Config Static(string address = "10.12.34.11/24", string? gateway = "10.12.34.1") =>
        new("static", address, gateway, new[] { "8.8.8.8" });

    private bool IsModify(string program, string[] args) => program == "nmcli" && args.Length > 1 && args[0] == "connection" && args[1] == "modify";

    [Theory]
    [InlineData("10.12.34.11/24", "10.12.34.1", null)]
    [InlineData("192.168.1.5/16", null, null)]
    [InlineData("10.12.34.11", null, "prefix")]
    [InlineData("10.12.34.11/0", null, "prefix")]
    [InlineData("10.12.34.11/31", null, "prefix")]
    [InlineData("not-an-ip/24", null, "IPv4")]
    [InlineData("127.0.0.1/8", null, "cannot be used")]
    [InlineData("224.1.1.1/24", null, "cannot be used")]
    [InlineData("10.12.34.11/24", "10.99.0.1", "subnet")]
    [InlineData("10.12.34.11/24", "gateway", "IPv4")]
    public void StaticSetupsAreChecked(string address, string? gateway, string? expectedProblem)
    {
        string? problem = NetworkService.ValidateIpv4(Static(address, gateway));

        if (expectedProblem == null) Assert.Null(problem);
        else Assert.Contains(expectedProblem, problem);
    }

    [Fact]
    public void ABadDnsServerAndAnUnknownMethodAreRefused()
    {
        Assert.Contains("DNS", NetworkService.ValidateIpv4(new Ipv4Config("static", "10.0.0.5/24", null, new[] { "dns.example" })));
        Assert.Contains("method", NetworkService.ValidateIpv4(new Ipv4Config("bridge", null, null, Array.Empty<string>())));
        Assert.Null(NetworkService.ValidateIpv4(new Ipv4Config("dhcp", null, null, Array.Empty<string>())));
    }

    [Theory]
    [InlineData("lumenvision", true)]
    [InlineData("front-camera-2", true)]
    [InlineData("Lumen", false)]
    [InlineData("-lumen", false)]
    [InlineData("lumen-", false)]
    [InlineData("lumen vision", false)]
    [InlineData("lumen; reboot", false)]
    [InlineData("", false)]
    public void HostnamesFollowTheDnsLabelRules(string name, bool valid)
    {
        Assert.Equal(valid, NetworkService.ValidateHostname(name) == null);
    }

    [Fact]
    public async Task ASetHostnamePassesTheNameAsOneArgument()
    {
        await Service().SetHostnameAsync("front-cam");

        Assert.Contains(runner.Calls, c => c.Program == "hostnamectl" && c.Arguments.SequenceEqual(new[] { "set-hostname", "front-cam" }));
        await Assert.ThrowsAsync<ApiException>(() => Service().SetHostnameAsync("bad name"));
        Assert.DoesNotContain(runner.Calls, c => c.Arguments.Contains("bad name"));
    }

    [Fact]
    public void NmcliTerseFieldsUnescapeColons()
    {
        Assert.Equal(new[] { "Wired:1", "802-3-ethernet", "end0" }, NetworkService.SplitTerse("Wired\\:1:802-3-ethernet:end0"));
    }

    [Fact]
    public async Task StatusListsEthernetConnectionsWithTheirSetupAndAddresses()
    {
        NetworkStatus status = await Service().GetStatusAsync();

        Assert.True(status.Supported);
        var connection = Assert.Single(status.Connections);
        Assert.Equal("Wired connection 1", connection.Name);
        Assert.Equal("end0", connection.Device);
        Assert.Equal("dhcp", connection.Configured.Method);
        Assert.Equal(new[] { "192.168.55.40/24" }, connection.CurrentAddresses);
        Assert.Null(status.Pending);
    }

    [Fact]
    public async Task WithoutNetworkManagerTheStatusSaysSo()
    {
        runner.Respond = (_, _) => new CommandResult(1, "", "NetworkManager is not running");

        NetworkStatus status = await Service().GetStatusAsync();

        Assert.False(status.Supported);
        await Assert.ThrowsAsync<ApiException>(() => Service().SetIpv4Async("Wired connection 1", Static()));
    }

    [Fact]
    public async Task AStaticChangeIsAppliedAndWaitsForConfirmation()
    {
        NetworkService service = Service();

        PendingNetworkChange pending = await service.SetIpv4Async("Wired connection 1", Static());

        Assert.Equal("Wired connection 1", pending.Connection);
        Assert.InRange(pending.SecondsLeft, 290, 300);
        var modify = runner.Calls.Single(c => IsModify(c.Program, c.Arguments));
        Assert.Contains("manual", modify.Arguments);
        Assert.Contains("10.12.34.11/24", modify.Arguments);
        Assert.Contains("10.12.34.1", modify.Arguments);
        Assert.True(File.Exists(pendingFile));

        // a second change is refused until the first is settled
        await Assert.ThrowsAsync<ApiException>(() => service.SetIpv4Async("Wired connection 1", Static("10.12.34.12/24")));

        service.Confirm();
        Assert.Null(service.CurrentPending());
        Assert.False(File.Exists(pendingFile));
    }

    [Fact]
    public async Task AnUnconfirmedChangeIsRevertedWhenTheCountdownEnds()
    {
        NetworkService service = Service(TimeSpan.FromMilliseconds(150));

        await service.SetIpv4Async("Wired connection 1", Static());
        await Task.Delay(800);

        var modifies = runner.Calls.Where(c => IsModify(c.Program, c.Arguments)).ToList();
        Assert.Equal(2, modifies.Count);
        Assert.Contains("auto", modifies[1].Arguments); // back to what it was: DHCP
        Assert.Null(service.CurrentPending());
        Assert.False(File.Exists(pendingFile));
    }

    [Fact]
    public async Task ARejectedChangeLeavesNothingPending()
    {
        runner.Respond = (program, args) =>
        {
            if (IsModify(program, args)) return new CommandResult(1, "", "invalid address");
            return new CommandResult(0, args.Contains("general") ? "connected\n" : "ipv4.method:auto\n", "");
        };
        NetworkService service = Service();

        await Assert.ThrowsAsync<ApiException>(() => service.SetIpv4Async("Wired connection 1", Static()));

        Assert.Null(service.CurrentPending());
        Assert.False(File.Exists(pendingFile));
    }

    [Fact]
    public async Task AChangeNobodyConfirmedBeforeARestartIsUndoneAtStartup()
    {
        // the previous server wrote this and died before the confirmation
        File.WriteAllText(pendingFile, """{"Connection":"Wired connection 1","Previous":{"Method":"dhcp","Address":null,"Gateway":null,"Dns":[]}}""");

        await Service().RecoverAsync();

        var modify = runner.Calls.Single(c => IsModify(c.Program, c.Arguments));
        Assert.Contains("auto", modify.Arguments);
        Assert.False(File.Exists(pendingFile));
    }
}
