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
        // Every thread must run at one priority, so the unit sets Nice=-19 for the whole service.
        Assert.Contains("\nNice=-19", UnitText().Replace("\r\n", "\n"));
    }
}
