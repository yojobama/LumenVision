using System;
using System.IO;
using System.Linq;
using Server;
using Xunit;

namespace Server.Tests;

public class LogTests : IDisposable
{
    private readonly string directory = Path.Combine(Path.GetTempPath(), $"lumen-logs-{Guid.NewGuid():N}");

    private readonly System.Collections.Generic.List<RollingLogFile> files = new();

    public void Dispose()
    {
        foreach (RollingLogFile file in files) file.Close();
        if (Directory.Exists(directory)) Directory.Delete(directory, true);
    }

    private LogHub Hub(int capacity = 100)
    {
        var file = new RollingLogFile(directory, keepDays: 3);
        files.Add(file);
        return new LogHub(file, capacity);
    }

    // the hub keeps its file open, so reading it needs a share mode that allows that
    private static string ReadAll(string path)
    {
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
        return new StreamReader(stream).ReadToEnd();
    }

    [Fact]
    public void EntriesAreFilteredByLevelAndTimeAndKeepTheirOrder()
    {
        LogHub hub = Hub();
        DateTime t0 = new(2026, 10, 7, 12, 0, 0, DateTimeKind.Utc);
        hub.Add(LogSeverity.Debug, "core", "noise", t0);
        hub.Add(LogSeverity.Info, "server", "started", t0.AddSeconds(1));
        hub.Add(LogSeverity.Warning, "core", "slow frame", t0.AddSeconds(2));
        hub.Add(LogSeverity.Error, "server", "boom", t0.AddSeconds(3));

        Assert.Equal(new[] { "slow frame", "boom" }, hub.Recent(10, LogSeverity.Warning).Select(e => e.Message).ToArray());
        Assert.Equal(new[] { "slow frame", "boom" }, hub.Recent(10, LogSeverity.Debug, t0.AddSeconds(2)).Select(e => e.Message).ToArray());
        Assert.Equal(new[] { "started", "slow frame", "boom" }, hub.Recent(3).Select(e => e.Message).ToArray());
        Assert.Equal(new[] { "boom" }, hub.Recent(1).Select(e => e.Message).ToArray());
    }

    [Fact]
    public void OnlyTheNewestEntriesAreKeptInMemoryButAllReachTheFile()
    {
        LogHub hub = Hub(capacity: 3);
        for (int i = 0; i < 10; i++) hub.Add(LogSeverity.Info, "server", $"line {i}");

        Assert.Equal(new[] { "line 7", "line 8", "line 9" }, hub.Recent(100).Select(e => e.Message).ToArray());
        string text = ReadAll(hub.File.Files().Single());
        Assert.Contains("line 0", text);
        Assert.Contains("line 9", text);
    }

    [Fact]
    public void FileLinesCarryTimeLevelAndSource()
    {
        LogHub hub = Hub();
        hub.Add(LogSeverity.Warning, "core", "camera lost", new DateTime(2026, 10, 7, 12, 30, 15, 123, DateTimeKind.Utc));

        Assert.Equal("2026-10-07T12:30:15.123Z [WARNING] core: camera lost", ReadAll(hub.File.Files().Single()).TrimEnd());
    }

    [Fact]
    public void OldDailyFilesAreDeleted()
    {
        Directory.CreateDirectory(directory);
        File.WriteAllText(Path.Combine(directory, "lumenvision-20260101.log"), "ancient");
        File.WriteAllText(Path.Combine(directory, "lumenvision-20261006.log"), "recent");
        LogHub hub = Hub();

        hub.Add(LogSeverity.Info, "server", "today", new DateTime(2026, 10, 7, 0, 0, 1, DateTimeKind.Utc));

        string[] names = hub.File.Files().Select(Path.GetFileName).ToArray()!;
        Assert.DoesNotContain("lumenvision-20260101.log", names);
        Assert.Contains("lumenvision-20261006.log", names);
        Assert.Contains("lumenvision-20261007.log", names);
    }

    [Theory]
    [InlineData("[INFO]: Manager constructed", LogSeverity.Info, "Manager constructed")]
    [InlineData("[WARNING]: Vulkan unavailable", LogSeverity.Warning, "Vulkan unavailable")]
    [InlineData("[ERROR]: camera grab failed", LogSeverity.Error, "camera grab failed")]
    [InlineData("[WTF]: impossible", LogSeverity.Error, "impossible")]
    [InlineData("[DEBUG]: per frame", LogSeverity.Debug, "per frame")]
    [InlineData("plain text", LogSeverity.Info, "plain text")]
    [InlineData("[3]: numbered", LogSeverity.Info, "[3]: numbered")]
    public void LumenCoreLogLinesAreParsed(string line, LogSeverity level, string message)
    {
        Assert.Equal((level, message), LogFileFollower.ParseLine(line));
    }

    [Fact]
    public void AFollowerReportsOnlyWhatIsAppendedAfterItStarted()
    {
        Directory.CreateDirectory(directory);
        string path = Path.Combine(directory, "core.log");
        File.WriteAllText(path, "[INFO]: old history\n");
        LogHub hub = Hub();
        var follower = new LogFileFollower(hub, path, "core");

        Assert.Equal(0, follower.Poll()); // the first look only notes where the end is
        File.AppendAllText(path, "[WARNING]: first\n[ERROR]: second\n");
        Assert.Equal(2, follower.Poll());
        Assert.Equal(0, follower.Poll());

        var entries = hub.Recent(10);
        Assert.Equal(new[] { "first", "second" }, entries.Select(e => e.Message).ToArray());
        Assert.All(entries, e => Assert.Equal("core", e.Source));
        Assert.Equal(LogSeverity.Error, entries[1].Level);
    }

    [Fact]
    public void AFollowerStartsOverWhenTheFileIsTruncated()
    {
        Directory.CreateDirectory(directory);
        string path = Path.Combine(directory, "core.log");
        File.WriteAllText(path, "[INFO]: a long first line of history\n");
        LogHub hub = Hub();
        var follower = new LogFileFollower(hub, path, "core");
        follower.Poll();

        File.WriteAllText(path, "[INFO]: new\n");

        Assert.Equal(1, follower.Poll());
        Assert.Equal("new", hub.Recent(1).Single().Message);
    }

    [Fact]
    public void AFollowerOfAMissingFileDoesNothing()
    {
        var follower = new LogFileFollower(Hub(), Path.Combine(directory, "missing.log"), "core");

        Assert.Equal(0, follower.Poll());
    }

    [Fact]
    public void TheConsoleTeeLogsWholeLinesAndSkipsTheFrameworksOwnFormatting()
    {
        LogHub hub = Hub();
        var original = new StringWriter();
        var tee = new ConsoleTee(original, hub, LogSeverity.Info);

        tee.Write("Loading data");
        tee.WriteLine("base...");
        tee.WriteLine("info: Microsoft.Hosting.Lifetime[14]");
        tee.WriteLine("      Now listening on: http://[::]:5800");
        tee.WriteLine("<6>Microsoft.AspNetCore.Hosting.Diagnostics[1] Request starting HTTP/1.1 GET /api/x");
        tee.WriteLine("[power] systemctl reboot failed: denied");
        tee.WriteLine("");

        Assert.Contains("Now listening", original.ToString()); // still printed
        var entries = hub.Recent(10);
        Assert.Equal("Loading database...", entries[0].Message);
        Assert.Equal("[power] systemctl reboot failed: denied", entries[1].Message);
        Assert.Equal(LogSeverity.Warning, entries[1].Level); // says "failed"
        Assert.Equal(2, entries.Count);
    }
}
