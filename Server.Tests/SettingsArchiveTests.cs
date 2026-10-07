using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Text.Json;
using Server;
using Server.Web;
using Xunit;

namespace Server.Tests;

public class SettingsArchiveTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), $"lumen-archive-{Guid.NewGuid():N}");

    public SettingsArchiveTests() => Directory.CreateDirectory(root);

    public void Dispose() => Directory.Delete(root, true);

    private SettingsArchive Archive() => new(new DeviceData(root), () => "9.9.9");

    private void Write(string relative, string text)
    {
        string path = Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, text);
    }

    private string Read(string relative) => File.ReadAllText(Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar)));

    private bool Exists(string relative) => File.Exists(Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar)));

    // a hand-made archive, so the tests can include entries the exporter would never write
    private static MemoryStream BuildArchive(string manifestJson, params (string Name, string Text)[] entries)
    {
        var stream = new MemoryStream();
        using (var zip = new ZipArchive(stream, ZipArchiveMode.Create, leaveOpen: true))
        {
            using (var writer = new StreamWriter(zip.CreateEntry("manifest.json").Open())) writer.Write(manifestJson);
            foreach (var (name, text) in entries)
                using (var writer = new StreamWriter(zip.CreateEntry(name).Open())) writer.Write(text);
        }
        stream.Position = 0;
        return stream;
    }

    private static string Manifest(string[] groups, params string[] files) =>
        JsonSerializer.Serialize(new ArchiveManifest(SettingsArchive.CurrentFormat, "1.0", DateTime.UtcNow, groups, files));

    [Fact]
    public void AnExportCarriesTheChosenGroupsAndAManifest()
    {
        Write("data.json", "graph");
        Write("settings.json", "settings");
        Write("calibrations.json", "cal");
        Write("models/1_yolo.onnx", "weights");
        Write("recordings/a.mp4", "video");
        Write("DBLog.txt", "log");

        using var output = new MemoryStream();
        Archive().Export(output, new[] { DataGroup.Configuration, DataGroup.Calibrations }, includeLog: false);
        output.Position = 0;
        using var zip = new ZipArchive(output);

        Assert.Equal(new[] { "calibrations.json", "data.json", "manifest.json", "settings.json" }, zip.Entries.Select(e => e.FullName).OrderBy(n => n, StringComparer.Ordinal).ToArray());
        ArchiveManifest manifest = JsonSerializer.Deserialize<ArchiveManifest>(new StreamReader(zip.GetEntry("manifest.json")!.Open()).ReadToEnd())!;
        Assert.Equal("9.9.9", manifest.ServerVersion);
        Assert.Equal(SettingsArchive.CurrentFormat, manifest.Format);
        Assert.Equal(new[] { "Configuration", "Calibrations" }, manifest.Groups);
    }

    [Fact]
    public void ModelsAndTheLogAreOptIn()
    {
        Write("data.json", "graph");
        Write("models/1_yolo.onnx", "weights");
        Write("DBLog.txt", "log");

        using var output = new MemoryStream();
        Archive().Export(output, new[] { DataGroup.Configuration, DataGroup.Models }, includeLog: true);
        output.Position = 0;
        using var zip = new ZipArchive(output);

        Assert.NotNull(zip.GetEntry("models/1_yolo.onnx"));
        Assert.NotNull(zip.GetEntry("DBLog.txt"));
    }

    [Fact]
    public void AnExportImportsBackIntoAnotherDevice()
    {
        Write("data.json", "graph v2");
        Write("graph-profiles/match.json", "profile");
        Write("fieldLayouts/sink-4.json", "layout");
        using var archive = new MemoryStream();
        Archive().Export(archive, new[] { DataGroup.Configuration }, includeLog: false);

        string other = Path.Combine(Path.GetTempPath(), $"lumen-archive-{Guid.NewGuid():N}");
        Directory.CreateDirectory(other);
        try
        {
            archive.Position = 0;
            ImportResult result = new SettingsArchive(new DeviceData(other), () => "9.9.9").Import(archive);

            Assert.Equal(3, result.FilesRestored);
            Assert.Equal("graph v2", File.ReadAllText(Path.Combine(other, "data.json")));
            Assert.Equal("profile", File.ReadAllText(Path.Combine(other, "graph-profiles", "match.json")));
        }
        finally
        {
            Directory.Delete(other, true);
        }
    }

    [Fact]
    public void ImportingReplacesTheGroupAndBacksUpWhatItReplaced()
    {
        Write("data.json", "old graph");
        Write("graph-profiles/old.json", "old profile");
        Write("calibrations.json", "keep me");
        using var archive = BuildArchive(Manifest(new[] { "Configuration" }, "data.json"), ("data.json", "new graph"));

        ImportResult result = Archive().Import(archive);

        Assert.Equal("new graph", Read("data.json"));
        Assert.False(Exists("graph-profiles/old.json"));   // the configuration group was replaced as a whole
        Assert.Equal("keep me", Read("calibrations.json")); // a group the archive lacks is left alone
        Assert.NotNull(result.BackupPath);
        using var backup = ZipFile.OpenRead(result.BackupPath!);
        Assert.Equal("old graph", new StreamReader(backup.GetEntry("data.json")!.Open()).ReadToEnd());
        Assert.NotNull(backup.GetEntry("graph-profiles/old.json"));
    }

    [Theory]
    [InlineData("../escape.txt")]
    [InlineData("data.json/../../escape.txt")]
    [InlineData("fieldLayouts/bundled/2026-rebuilt-welded.json")]
    [InlineData("recordings/a.mp4")]
    [InlineData("Server.dll")]
    [InlineData("graph-profiles/notjson.exe")]
    public void EntriesOutsideTheKnownDataAreRefusedAndNothingChanges(string name)
    {
        Write("data.json", "untouched");
        using var archive = BuildArchive(Manifest(new[] { "Configuration" }, "data.json", name), ("data.json", "new"), (name, "evil"));

        var ex = Assert.Throws<ApiException>(() => Archive().Import(archive));

        Assert.Equal(400, ex.StatusCode);
        Assert.Equal("untouched", Read("data.json"));
        Assert.False(File.Exists(Path.Combine(Path.GetTempPath(), "escape.txt")));
    }

    [Fact]
    public void AnEntryMissingFromTheManifestIsRefused()
    {
        using var archive = BuildArchive(Manifest(new[] { "Configuration" }, "data.json"), ("data.json", "x"), ("settings.json", "sneaky"));

        Assert.Equal(400, Assert.Throws<ApiException>(() => Archive().Import(archive)).StatusCode);
    }

    [Fact]
    public void ForeignOrBrokenArchivesAreRefused()
    {
        Assert.Equal(400, Assert.Throws<ApiException>(() => Archive().Import(new MemoryStream(Encoding.UTF8.GetBytes("not a zip")))).StatusCode);

        using var noManifest = new MemoryStream();
        using (var zip = new ZipArchive(noManifest, ZipArchiveMode.Create, leaveOpen: true))
            using (var writer = new StreamWriter(zip.CreateEntry("data.json").Open())) writer.Write("x");
        noManifest.Position = 0;
        Assert.Equal(400, Assert.Throws<ApiException>(() => Archive().Import(noManifest)).StatusCode);

        using var wrongFormat = BuildArchive("""{"Format":99,"ServerVersion":"1","CreatedUtc":"2026-01-01T00:00:00Z","Groups":[],"Files":[]}""");
        Assert.Contains("format", Assert.Throws<ApiException>(() => Archive().Import(wrongFormat)).Message);
    }

    [Fact]
    public void OnlyTheNewestBackupsAreKept()
    {
        Write("data.json", "graph");
        for (int i = 0; i < 8; i++)
        {
            using var archive = BuildArchive(Manifest(new[] { "Configuration" }, "data.json"), ("data.json", $"v{i}"));
            Archive().Import(archive);
        }

        Assert.Equal(5, Directory.GetFiles(Path.Combine(root, "backups"), "before-import-*.zip").Length);
    }
}
