using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Server;
using Xunit;

namespace Server.Tests;

// What the device operations run, recorded instead of executed.
public sealed class FakeRunner : IPrivilegedRunner
{
    public List<(string Program, string[] Arguments)> Calls { get; } = new();
    public Func<string, string[], CommandResult> Respond { get; set; } = (_, _) => new CommandResult(0, "", "");

    public Task<CommandResult> RunAsync(string program, IReadOnlyList<string> arguments, TimeSpan? timeout = null, CancellationToken cancellationToken = default, bool quiet = false)
    {
        var args = new List<string>(arguments).ToArray();
        lock (Calls) Calls.Add((program, args));
        return Task.FromResult(Respond(program, args));
    }
}

public class PrivilegedCommandTests
{
    [Fact]
    public void ArgumentsStaySeparateItemsAfterANonInteractiveSudo()
    {
        var info = SudoRunner.BuildStartInfo("hostnamectl", new[] { "set-hostname", "evil; reboot", "$(id)" });

        Assert.Equal("sudo", info.FileName);
        Assert.Equal(new[] { "-n", "--", "hostnamectl", "set-hostname", "evil; reboot", "$(id)" }, info.ArgumentList);
        Assert.False(info.UseShellExecute);
    }

    [Fact]
    public async Task TheRunnerCanBeReplacedForTests()
    {
        var original = PrivilegedCommand.Runner;
        var fake = new FakeRunner();
        PrivilegedCommand.Runner = fake;
        try
        {
            var result = await PrivilegedCommand.RunAsync("systemctl", "restart", "lumenvision");

            Assert.True(result.Ok);
            Assert.Equal("systemctl", fake.Calls[0].Program);
            Assert.Equal(new[] { "restart", "lumenvision" }, fake.Calls[0].Arguments);
        }
        finally
        {
            PrivilegedCommand.Runner = original;
        }
    }

    [Fact]
    public async Task SudoIsRefusedOffTheCoprocessor()
    {
        if (System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.Linux)) return;
        await Assert.ThrowsAsync<PlatformNotSupportedException>(() => new SudoRunner().RunAsync("true", Array.Empty<string>()));
    }
}
