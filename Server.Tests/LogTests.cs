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
        File.WriteAllText(Path.Combine(directory, "lumenvision-20260101-001.log"), "ancient");
        File.WriteAllText(Path.Combine(directory, "lumenvision-20261006-001.log"), "recent");
        LogHub hub = Hub();

        hub.Add(LogSeverity.Info, "server", "today", new DateTime(2026, 10, 7, 0, 0, 1, DateTimeKind.Utc));

        string[] names = hub.File.Files().Select(Path.GetFileName).ToArray()!;
        Assert.DoesNotContain("lumenvision-20260101-001.log", names);
        Assert.Contains("lumenvision-20261006-001.log", names);
        Assert.Contains("lumenvision-20261007-001.log", names);
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
        var tee = new ConsoleTee(original, hub, LogSeverity.Info, passThrough: true);

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

    // ---- size and retention limits ----

    private static string Line(int i) => $"entry {i} " + new string('x', 190);

    [Fact]
    public void ADayRollsIntoNumberedPartsAtTheFileSizeLimit()
    {
        var file = new RollingLogFile(directory, new LogLimits(20_000, 10_000_000, 7));
        files.Add(file);
        var hub = new LogHub(file, 100);
        DateTime day = new(2026, 10, 7, 12, 0, 0, DateTimeKind.Utc);

        for (int i = 0; i < 400; i++) hub.Add(LogSeverity.Info, "server", Line(i), day);

        string[] names = file.Files().Select(Path.GetFileName).ToArray()!;
        Assert.True(names.Length >= 4, string.Join(",", names));
        Assert.Equal(names.OrderBy(n => n, StringComparer.Ordinal), names); // part numbers keep the names in time order
        Assert.All(file.Files(), f => Assert.True(new FileInfo(f).Length < 20_000 + 400));
        Assert.StartsWith("lumenvision-20261007-001", names[0]);
        // nothing was dropped: every entry is in some part
        Assert.Equal(400, file.Files().Sum(f => ReadAll(f).Split('\n', StringSplitOptions.RemoveEmptyEntries).Length));
    }

    [Fact]
    public void TheFolderStaysUnderItsBudgetByDeletingTheOldestFilesFirst()
    {
        var file = new RollingLogFile(directory, new LogLimits(20_000, 70_000, 7));
        files.Add(file);
        var hub = new LogHub(file, 100);
        DateTime day = new(2026, 10, 7, 12, 0, 0, DateTimeKind.Utc);

        for (int i = 0; i < 2000; i++) hub.Add(LogSeverity.Info, "server", Line(i), day);

        Assert.True(file.TotalBytes() <= 70_000 + 20_400, file.TotalBytes().ToString());
        string newest = ReadAll(file.Files().Last());
        Assert.Contains("entry 1999 ", newest);
        Assert.DoesNotContain(file.Files(), f => ReadAll(f).Contains("entry 0 "));
    }

    [Fact]
    public void ALowerBudgetTrimsTheFolderAtOnce()
    {
        var file = new RollingLogFile(directory, new LogLimits(20_000, 10_000_000, 7));
        files.Add(file);
        var hub = new LogHub(file, 100);
        DateTime day = new(2026, 10, 7, 12, 0, 0, DateTimeKind.Utc);
        for (int i = 0; i < 1000; i++) hub.Add(LogSeverity.Info, "server", Line(i), day);
        long before = file.TotalBytes();

        file.Configure(new LogLimits(20_000, 50_000, 7));

        Assert.True(file.TotalBytes() < before);
        Assert.True(file.TotalBytes() <= 50_000 + 20_400);
    }

    [Fact]
    public void AfterARestartTheDaysLastPartIsContinuedNotOverwritten()
    {
        var first = new RollingLogFile(directory, LogLimits.Default);
        files.Add(first);
        new LogHub(first, 10).Add(LogSeverity.Info, "server", "before restart", new DateTime(2026, 10, 7, 9, 0, 0, DateTimeKind.Utc));
        first.Close();

        var second = new RollingLogFile(directory, LogLimits.Default);
        files.Add(second);
        new LogHub(second, 10).Add(LogSeverity.Info, "server", "after restart", new DateTime(2026, 10, 7, 9, 5, 0, DateTimeKind.Utc));

        string only = ReadAll(Assert.Single(second.Files()));
        Assert.Contains("before restart", only);
        Assert.Contains("after restart", only);
    }

    [Fact]
    public void EntriesReadFromAnotherLogFileAreKeptInMemoryButNotWrittenAgain()
    {
        LogHub hub = Hub();

        hub.Add(LogSeverity.Info, "core", "already in LumenVision.log", persist: false);
        hub.Add(LogSeverity.Info, "server", "the server's own");

        Assert.Equal(2, hub.Recent(10).Count);
        string written = ReadAll(Assert.Single(hub.File.Files()));
        Assert.Contains("the server's own", written);
        Assert.DoesNotContain("already in LumenVision.log", written);
    }

    [Fact]
    public void EveryRepeatedEntryIsStillLogged()
    {
        LogHub hub = Hub();

        for (int i = 0; i < 300; i++) hub.Add(LogSeverity.Error, "server", "camera grab failed");

        Assert.Equal(300, ReadAll(Assert.Single(hub.File.Files())).Split('\n', StringSplitOptions.RemoveEmptyEntries).Length);
    }

    [Fact]
    public void StdoutIsNotALogDestinationButTheTextStillReachesTheLog()
    {
        LogHub hub = Hub();
        var stdout = new StringWriter();
        var tee = new ConsoleTee(stdout, hub, LogSeverity.Info, passThrough: false);

        tee.WriteLine("Loading database...");
        tee.Flush();

        Assert.Equal("", stdout.ToString());
        Assert.Equal("Loading database...", hub.Recent(1).Single().Message);
    }

    // ---- retention settings, usage and clearing ----

    [Theory]
    [InlineData(0, 3, 50, 7)]
    [InlineData(2000, 3, 50, 7)]
    [InlineData(10, -1, 50, 7)]
    [InlineData(10, 21, 50, 7)]
    [InlineData(10, 3, 5, 7)]   // the budget cannot be smaller than one file
    [InlineData(10, 3, 50, 0)]
    [InlineData(10, 3, 50, 400)]
    public void ImplausibleRetentionSettingsAreRefused(int fileMb, int kept, int budgetMb, int days)
    {
        var settings = new LogSettings { MaxFileMb = fileMb, FilesKept = kept, ServerBudgetMb = budgetMb, KeepDays = days };

        Assert.NotNull(LogRetention.Validate(settings));
        Assert.NotNull(DeviceSettings.Validate(new DeviceSettingsData
        {
            NetworkTables = new NetworkTablesSettings { Mode = "team", TeamNumber = 1234 },
            Logs = settings,
        }));
    }

    [Fact]
    public void TheDefaultsAreValidAndBounded()
    {
        var defaults = new LogSettings();

        Assert.Null(LogRetention.Validate(defaults));
        Assert.Equal(new LogLimits(10L * 1024 * 1024, 50L * 1024 * 1024, 7), LogRetention.ServerLimits(defaults));
    }

    [Fact]
    public void UsageCountsEachKindOfLogWithItsRotatedCopies()
    {
        Directory.CreateDirectory(directory);
        File.WriteAllText(Path.Combine(directory, "LumenVision.log"), new string('a', 1000));
        File.WriteAllText(Path.Combine(directory, "LumenVision.log.1"), new string('a', 500));
        File.WriteAllText(Path.Combine(directory, "LumenVision.log.2"), new string('a', 250));
        File.WriteAllText(Path.Combine(directory, "LumenVision.log.bak"), new string('a', 9999)); // not a rotation
        File.WriteAllText(Path.Combine(directory, "DBLog.txt"), new string('b', 100));
        LogHub hub = Hub();
        hub.Add(LogSeverity.Info, "server", "x");

        LogUsage usage = LogRetention.Usage(directory, hub);

        Assert.Equal(1750, usage.CoreBytes);
        Assert.Equal(100, usage.DatabaseBytes);
        Assert.True(usage.ServerBytes > 0);
        Assert.Equal(usage.CoreBytes + usage.DatabaseBytes + usage.ServerBytes, usage.TotalBytes);
        Assert.Equal(5, usage.Files); // three LumenCore files, the store log and one server file
    }

    [Fact]
    public void ClearingEmptiesTheLiveFilesAndDeletesTheRest()
    {
        Directory.CreateDirectory(directory);
        string live = Path.Combine(directory, "LumenVision.log");
        File.WriteAllText(live, new string('a', 1000));
        File.WriteAllText(live + ".1", "old");
        LogHub hub = Hub();
        hub.Add(LogSeverity.Info, "server", "x");

        int cleared = LogRetention.Clear(directory, hub);

        Assert.True(cleared >= 3);
        Assert.True(File.Exists(live));            // LumenCore holds it open: emptied, not deleted
        Assert.Equal(0, new FileInfo(live).Length);
        Assert.False(File.Exists(live + ".1"));
        Assert.Empty(hub.File.Files());
        Assert.Empty(hub.Recent(10));
    }
}
