using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using Server;
using Xunit;

namespace Server.Tests;

// The saved-configuration fixtures the node-API migration is tested against must stay loadable by the current schema.
public class ConfigFixtureTests
{
    private static JsonElement Load(string name) =>
        JsonDocument.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "fixtures", name))).RootElement;

    [Fact]
    public void Config_without_profiles_deserialises()
    {
        JsonElement root = Load("data-no-profiles.json");
        Source camera = JsonSerializer.Deserialize<Source>(root.GetProperty("Sources")[0].GetRawText())!;
        Assert.Equal(SourceType.Camera, camera.Type);
        Assert.Empty(camera.Profiles);
        Assert.Equal(-1, camera.ActiveProfileIndex);
        Assert.NotEmpty(root.GetProperty("Sinks").EnumerateArray());
    }

    [Fact]
    public void Config_with_profiles_covers_every_profile_field()
    {
        JsonElement root = Load("data-with-profiles.json");
        Source camera = JsonSerializer.Deserialize<Source>(root.GetProperty("Sources")[0].GetRawText())!;
        Assert.Equal(2, camera.Profiles.Count);
        Assert.Equal(2, camera.ActiveProfileIndex);
        Assert.Equal(777, camera.ActiveDetectionSinkId);

        PipelineProfile tags = camera.Profiles.Single(p => p.Index == 0);
        Assert.Equal(DetectionSinkKind.ApriltagSink, tags.Kind);
        Assert.Equal("field-layouts/profile-0.json", tags.FieldLayoutPath);
        Assert.Equal(100, tags.CameraOverrides!.ControlValues[9963776]);
        Assert.Equal(30, tags.CameraOverrides.FpsLimit);

        PipelineProfile notes = camera.Profiles.Single(p => p.Index == 2);
        Assert.Equal(DetectionSinkKind.ObjectDetectionSink, notes.Kind);
        Assert.True(notes.DriverMode);

        Sink active = JsonSerializer.Deserialize<Sink>(
            root.GetProperty("Sinks").EnumerateArray().Single(s => s.GetProperty("Id").GetInt32() == 777).GetRawText())!;
        Assert.Equal(SinkType.ObjectDetectionSink, active.Type);
    }
}
