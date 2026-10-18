using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;
using Server;
using Xunit;

namespace Server.Tests;

public class NodeMigrationTests
{
    private static string Fixture(string name) => File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "fixtures", name));

    private static Func<int> Ids() { int next = 5000; return () => next++; }

    private static JsonObject Camera(JsonObject root) => (JsonObject)root["Sources"]![0]!;

    private static JsonObject Sink(JsonObject root, int id) =>
        root["Sinks"]!.AsArray().OfType<JsonObject>().Single(s => s["Id"]!.GetValue<int>() == id);

    private static List<int> Disabled(JsonObject root) => root["DisabledNodes"]!.AsArray().Select(n => n!.GetValue<int>()).ToList();

    [Fact]
    public void AConfigWithoutProfilesIsLeftAlone()
    {
        Assert.False(NodeMigration.Needed(Fixture("data-no-profiles.json")));
    }

    [Fact]
    public void EachProfileBecomesADetectorNodeAndOnlyTheActiveOneStaysOn()
    {
        string json = Fixture("data-with-profiles.json");
        Assert.True(NodeMigration.Needed(json));

        var result = NodeMigration.Migrate(json, Ids());
        var root = (JsonObject)JsonNode.Parse(result.Json)!;

        // the active profile ('notes', driver mode on) keeps its existing node 777; 'tags' gets a new one
        JsonObject tags = Sink(root, 5000);
        Assert.Equal("LenovoCamera0 - tags", tags["Name"]!.GetValue<string>());
        Assert.Equal((int)SinkType.ApriltagSink, tags["Type"]!.GetValue<int>());
        Assert.Equal(0.1651, tags["ApriltagTagSize"]!.GetValue<double>());
        Assert.Equal(Camera(root)["Id"]!.GetValue<int>(), tags["Source"]!["Id"]!.GetValue<int>());
        Assert.Equal(Sink(root, 777)["Source"]!["Id"]!.GetValue<int>(), tags["Source"]!["Id"]!.GetValue<int>());

        // 'notes' is the active profile but had driver mode on, so it is saved stopped like the inactive 'tags'
        Assert.Equal(new List<int> { 777, 5000 }, Disabled(root));
        Assert.Contains(result.LayoutCopies, c => c.NodeId == 5000 && c.FromPath == "field-layouts/profile-0.json");
        Assert.Contains(result.Notes, n => n.Contains("'tags'"));

        Assert.False(Camera(root).ContainsKey("Profiles"));
        Assert.False(Camera(root).ContainsKey("ActiveProfileIndex"));
        Assert.False(Sink(root, 777)["Source"]!.AsObject().ContainsKey("Profiles"));
    }

    [Fact]
    public void TheActiveProfilesCameraSettingsBecomeTheCamerasOwn()
    {
        var root = (JsonObject)JsonNode.Parse(Fixture("data-with-profiles.json"))!;
        JsonObject camera = Camera(root);
        camera["ActiveProfileIndex"] = 0; // 'tags', which has camera overrides and driver mode off

        var migrated = (JsonObject)JsonNode.Parse(NodeMigration.Migrate(root.ToJsonString(), Ids()).Json)!;

        Assert.Equal(100, Camera(migrated)["ControlValues"]!["9963776"]!.GetValue<int>());
        Assert.Equal(30, Camera(migrated)["FpsLimit"]!.GetValue<int>());
    }

    [Fact]
    public void AMigratedConfigIsNotMigratedAgainAndLoadsAsSourcesAndSinks()
    {
        string once = NodeMigration.Migrate(Fixture("data-with-profiles.json"), Ids()).Json;

        Assert.False(NodeMigration.Needed(once));

        JsonElement root = JsonDocument.Parse(once).RootElement;
        Source camera = JsonSerializer.Deserialize<Source>(root.GetProperty("Sources")[0].GetRawText())!;
        Assert.Equal(SourceType.Camera, camera.Type);
        Sink tags = JsonSerializer.Deserialize<Sink>(root.GetProperty("Sinks").EnumerateArray().Single(s => s.GetProperty("Id").GetInt32() == 5000).GetRawText())!;
        Assert.Equal(0.1651, tags.ApriltagTagSize);
    }
}
