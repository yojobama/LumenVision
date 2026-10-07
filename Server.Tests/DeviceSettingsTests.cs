using System.Text.Json;
using Server;
using Xunit;

namespace Server.Tests;

[Collection("ServerSingletons")]
public class DeviceSettingsTests
{
    private static DeviceSettingsData Good() => new()
    {
        NetworkTables = new NetworkTablesSettings { Mode = "team", TeamNumber = 1234 },
    };

    [Fact]
    public void ReasonableSettingsAreAccepted()
    {
        Assert.Null(DeviceSettings.Validate(Good()));
        Assert.Null(DeviceSettings.Validate(new DeviceSettingsData { NetworkTables = new NetworkTablesSettings { Mode = "server", ServerAddress = "10.12.34.2", Port = 5810 } }));
    }

    [Theory]
    [InlineData("team", null, null, 0, "lumenvision", "a team number")]
    [InlineData("team", 0, null, 0, "lumenvision", "a team number")]
    [InlineData("team", 99999, null, 0, "lumenvision", "a team number")]
    [InlineData("server", null, "", 0, "lumenvision", "server address")]
    [InlineData("robot", 1234, null, 0, "lumenvision", "mode")]
    [InlineData("team", 1234, null, 70000, "lumenvision", "port")]
    [InlineData("team", 1234, null, 0, "bad table", "root table")]
    [InlineData("team", 1234, null, 0, "../x", "root table")]
    public void InvalidSettingsAreRefusedWithAnExplanation(string mode, int? team, string? address, int port, string root, string expected)
    {
        var data = new DeviceSettingsData
        {
            NetworkTables = new NetworkTablesSettings { Mode = mode, TeamNumber = team, ServerAddress = address, Port = port, RootTable = root },
        };

        string? problem = DeviceSettings.Validate(data);

        Assert.NotNull(problem);
        Assert.Contains(expected, problem);
    }

    [Fact]
    public void SettingsSavedBeforeNetworkTablesWereAddedStillLoad()
    {
        var restored = JsonSerializer.Deserialize<DeviceSettingsData>("""{"Led":{"Enabled":true,"Chip":1,"Line":4,"ActiveLow":false}}""")!;

        Assert.True(restored.Led.Enabled);
        Assert.Equal("team", restored.NetworkTables.Mode);
        Assert.Equal("lumenvision", restored.NetworkTables.RootTable);
    }

    [Fact]
    public void AnNtSinkIsRebuiltWithTheNewSettingsAndKeepsItsId()
    {
        DeviceSettingsData original = DeviceSettings.Instance.Data;
        int id = -1;
        try
        {
            DeviceSettings.Instance.Replace(new DeviceSettingsData
            {
                NetworkTables = new NetworkTablesSettings { Mode = "server", ServerAddress = "127.0.0.1", Port = 5899, RootTable = "before" },
            });
            id = SinkManager.Instance.AddNetworkTablesSinkFromSettings("nt");
            Assert.Equal("before", NetworkTablesStatusDto.Parse(SinkManager.Instance.GetNetworkTablesSinkStatus(id)).RootTable);

            DeviceSettings.Instance.Replace(new DeviceSettingsData
            {
                NetworkTables = new NetworkTablesSettings { Mode = "server", ServerAddress = "127.0.0.1", Port = 5899, RootTable = "after" },
            });
            SinkManager.Instance.ReapplyNetworkTablesSettings();

            Assert.Equal("after", NetworkTablesStatusDto.Parse(SinkManager.Instance.GetNetworkTablesSinkStatus(id)).RootTable);
            Assert.NotNull(SinkManager.Instance.GetSinkById(id));
        }
        finally
        {
            if (id >= 0) SinkManager.Instance.DeleteSink(id);
            DeviceSettings.Instance.Replace(original);
        }
    }

    [Fact]
    public void WithoutATeamNumberASettingsSinkIsRefused()
    {
        DeviceSettingsData original = DeviceSettings.Instance.Data;
        try
        {
            DeviceSettings.Instance.Replace(new DeviceSettingsData());
            var ex = Assert.Throws<Server.Web.ApiException>(() => SinkManager.Instance.AddNetworkTablesSinkFromSettings("nt"));
            Assert.Equal(400, ex.StatusCode);
        }
        finally
        {
            DeviceSettings.Instance.Replace(original);
        }
    }
}
