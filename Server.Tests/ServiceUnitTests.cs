using System;
using System.IO;
using Xunit;

namespace Server.Tests;

public class ServiceUnitTests
{
    private static string UnitText()
    {
        for (DirectoryInfo? dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
        {
            string candidate = Path.Combine(dir.FullName, "scripts", "lumenvision.service");
            if (File.Exists(candidate)) return File.ReadAllText(candidate);
        }
        throw new FileNotFoundException("scripts/lumenvision.service was not found above " + AppContext.BaseDirectory);
    }

    [Fact]
    public void TheServiceRunsEveryThreadAtOnePriority()
    {
        // A thread inherits its creator's nice value, and the NPU runtime raises the thread that loads a model to -19, so without this a detector
        // restarted from a web request ran at nice 0 beside boot-time ones at -19 and came out 30-60% slower (see docs/PERFORMANCE_ANALYSIS.md).
        Assert.Contains("\nNice=-19", UnitText().Replace("\r\n", "\n"));
    }
}
