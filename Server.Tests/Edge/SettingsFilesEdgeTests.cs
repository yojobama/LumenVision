using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Text.Json;
using Server;
using Server.Web;
using Xunit;

namespace Server.Tests.Edge;

// Device settings and settings archives that are corrupt, partial or hand-made.
public class SettingsFilesEdgeTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), $"lumen-edge-f-{Guid.NewGuid():N}");

    public SettingsFilesEdgeTests() => Directory.CreateDirectory(root);

    public void Dispose() => Directory.Delete(root, true);

    private string PathOf(string relative) => Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar));

    private void Write(string relative, string text)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(PathOf(relative))!);
        File.WriteAllText(PathOf(relative), text);
    }

    // F7: PUT device/settings accepts {"Led": null} (Validate substitutes defaults only to check it) and stores the null; settings.json then
    // holds "Led": null, and LedController dereferences Data.Led (SettingsChanged right away -> 500; its static initialiser after a restart ->
    // TypeInitializationException on every LED use). A settings.json with a null section (hand edit, restored archive) loads the same way.
    [Fact]
    [Trait("Category", "Edge")]
    public void F_ANullSettingsSectionIsNeverStoredOrLoaded()
    {
        string path = PathOf("settings.json");
        var settings = new DeviceSettings(path);
        DeviceSettingsData next = JsonSerializer.Deserialize<DeviceSettingsData>(
            """{"Led":null,"Logs":null,"NetworkTables":{"Mode":"team","TeamNumber":1234,"RootTable":"lumenvision","ClientIdentity":"lumenvision"}}""")!;

        if (DeviceSettings.Validate(next) == null) settings.Replace(next);
        Assert.NotNull(settings.Data.Led);

        File.WriteAllText(path, """{"Led":null,"NetworkTables":null,"Logs":null}""");
        DeviceSettingsData loaded = new DeviceSettings(path).Data;
        Assert.NotNull(loaded.Led);
        Assert.NotNull(loaded.NetworkTables);
    }

    // a hand-made archive whose entries are compressed (Optimal) so their data can be corrupted afterwards
    private static MemoryStream BuildArchive(string manifestJson, params (string Name, string Text)[] entries)
    {
        var stream = new MemoryStream();
        using (var zip = new ZipArchive(stream, ZipArchiveMode.Create, leaveOpen: true))
        {
            using (var writer = new StreamWriter(zip.CreateEntry(SettingsArchive.ManifestName).Open())) writer.Write(manifestJson);
            foreach (var (name, text) in entries)
                using (var writer = new StreamWriter(zip.CreateEntry(name, CompressionLevel.Optimal).Open())) writer.Write(text);
        }
        stream.Position = 0;
        return stream;
    }

    // Overwrites the compressed data of one entry with 0xFF (a deflate block of the reserved type 3): the central directory, names and sizes stay
    // valid, so Validate passes, and only reading the entry's data fails.
    private static void CorruptEntryData(MemoryStream zip, string name)
    {
        byte[] bytes = zip.ToArray();
        byte[] nameBytes = Encoding.UTF8.GetBytes(name);
        for (int i = 0; i + 30 < bytes.Length; i++)
        {
            if (bytes[i] != 0x50 || bytes[i + 1] != 0x4B || bytes[i + 2] != 0x03 || bytes[i + 3] != 0x04) continue;
            int nameLength = BitConverter.ToUInt16(bytes, i + 26);
            int extraLength = BitConverter.ToUInt16(bytes, i + 28);
            int compressedSize = BitConverter.ToInt32(bytes, i + 18);
            if (nameLength != nameBytes.Length || !bytes.AsSpan(i + 30, nameLength).SequenceEqual(nameBytes)) continue;
            int data = i + 30 + nameLength + extraLength;
            for (int j = 0; j < compressedSize; j++) bytes[data + j] = 0xFF;
            zip.Position = 0;
            zip.Write(bytes, 0, bytes.Length);
            zip.Position = 0;
            return;
        }
        throw new InvalidOperationException($"no local header for {name}");
    }

    // F8: Import deletes every file of the groups it replaces and only then extracts; an entry whose data cannot be read (a damaged download)
    // throws InvalidDataException part-way, so the device is left with part of the archive and without the rest of its own configuration
    // (settings.json, saved graph profiles, uploaded field layouts) - and a 500 instead of the 400 "a bad archive changes nothing".
    [Fact]
    [Trait("Category", "Edge")]
    public void F_AnArchiveWithADamagedEntryChangesNothing()
    {
        Write("data.json", "old graph");
        Write("settings.json", "old settings");
        Write("graph-profiles/match.json", "old profile");
        string longText = string.Concat(Enumerable.Repeat("{\"Sinks\":[],\"Sources\":[]}\n", 200));
        string manifest = JsonSerializer.Serialize(new ArchiveManifest(SettingsArchive.CurrentFormat, "1.0", DateTime.UtcNow, new[] { "Configuration" },
            new[] { "data.json", "graph-profiles/new.json" }));
        using MemoryStream archive = BuildArchive(manifest, ("data.json", longText), ("graph-profiles/new.json", longText));
        CorruptEntryData(archive, "graph-profiles/new.json");

        Exception? import = Record.Exception(() => new SettingsArchive(new DeviceData(root), () => "9.9.9").Import(archive));

        Assert.NotNull(import); // the import did fail (a successful one replaces the group as a whole, by design)
        Assert.True(File.Exists(PathOf("settings.json")), $"settings.json was deleted by an import that failed with {import!.GetType().Name}: {import.Message}");
        Assert.True(File.Exists(PathOf("graph-profiles/match.json")), "a saved graph profile was deleted by a failed import");
        Assert.Equal("old graph", File.ReadAllText(PathOf("data.json")));
        Assert.Equal(400, Assert.IsType<ApiException>(import).StatusCode);
    }

    // F9: a manifest without "Files" (or with "Files": null) deserialises (the record has no required members) and Validate's
    // new HashSet<string>(manifest.Files) throws ArgumentNullException - a 500 rather than the 400 the validation promises.
    [Theory]
    [Trait("Category", "Edge")]
    [InlineData("""{"Format":1,"ServerVersion":"1","CreatedUtc":"2026-01-01T00:00:00Z","Groups":["Configuration"],"Files":null}""")]
    [InlineData("""{"Format":1}""")]
    public void F_AManifestWithMissingListsIsA400(string manifest)
    {
        Write("data.json", "untouched");
        using MemoryStream archive = BuildArchive(manifest);

        Exception? import = Record.Exception(() => new SettingsArchive(new DeviceData(root), () => "9.9.9").Import(archive));

        Assert.Equal("untouched", File.ReadAllText(PathOf("data.json")));
        Assert.Equal(400, Assert.IsType<ApiException>(import).StatusCode);
    }
}
