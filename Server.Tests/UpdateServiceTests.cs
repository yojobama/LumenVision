using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Server;
using Server.Web;
using Xunit;

namespace Server.Tests;

public class UpdateServiceTests : IDisposable
{
    private readonly string directory = Path.Combine(Path.GetTempPath(), $"lumen-update-{Guid.NewGuid():N}");
    private readonly FakeRunner runner = new();

    public UpdateServiceTests()
    {
        runner.Respond = (program, args) => program switch
        {
            "dpkg-deb" => new CommandResult(0, "Package: lumenvision-backend\nVersion: 1.4.0\nArchitecture: arm64\n", ""),
            "dpkg" => new CommandResult(0, "arm64\n", ""),
            _ => new CommandResult(0, "", ""),
        };
    }

    public void Dispose()
    {
        if (Directory.Exists(directory)) Directory.Delete(directory, true);
    }

    private UpdateService Service() => new(() => runner, directory);

    private static MemoryStream Deb() => new(Encoding.ASCII.GetBytes("!<arch>\ndebian-binary   0 0 0 100644 4 `\n2.0\n"));

    [Fact]
    public async Task APackageForThisBoardIsStagedAndDescribed()
    {
        StagedPackage staged = await Service().StageAsync(Deb());

        Assert.Equal("lumenvision-backend", staged.Package);
        Assert.Equal("1.4.0", staged.Version);
        Assert.Equal("arm64", staged.Architecture);
        Assert.True(File.Exists(Service().PackagePath));
    }

    [Fact]
    public async Task AFileThatIsNotADebIsRefusedWithoutRunningAnything()
    {
        var ex = await Assert.ThrowsAsync<ApiException>(() => Service().StageAsync(new MemoryStream(Encoding.ASCII.GetBytes("MZ not a deb"))));

        Assert.Equal(400, ex.StatusCode);
        Assert.Empty(runner.Calls);
        Assert.False(File.Exists(Service().PackagePath));
    }

    [Fact]
    public async Task AnotherPackageOrAnotherArchitectureIsRefusedAndDeleted()
    {
        runner.Respond = (program, _) => program == "dpkg-deb"
            ? new CommandResult(0, "Package: openssh-server\nVersion: 9\nArchitecture: arm64\n", "")
            : new CommandResult(0, "arm64\n", "");
        Assert.Contains("openssh-server", (await Assert.ThrowsAsync<ApiException>(() => Service().StageAsync(Deb()))).Message);
        Assert.False(File.Exists(Service().PackagePath));

        runner.Respond = (program, _) => program == "dpkg-deb"
            ? new CommandResult(0, "Package: lumenvision-backend\nVersion: 1\nArchitecture: amd64\n", "")
            : new CommandResult(0, "arm64\n", "");
        Assert.Contains("amd64", (await Assert.ThrowsAsync<ApiException>(() => Service().StageAsync(Deb()))).Message);
        Assert.False(File.Exists(Service().PackagePath));
    }

    [Fact]
    public async Task InstallingNeedsAnUploadedPackage()
    {
        await Assert.ThrowsAsync<ApiException>(() => Service().StartAsync());
    }

    [Fact]
    public async Task TheInstallRunsInItsOwnUnitWithThePathsAsPositionalArguments()
    {
        UpdateService service = Service();
        await service.StageAsync(Deb());
        runner.Calls.Clear();

        await service.StartAsync();

        var call = Assert.Single(runner.Calls);
        Assert.Equal("systemd-run", call.Program);
        Assert.Contains("--unit=lumenvision-update", call.Arguments);
        Assert.Contains("--collect", call.Arguments);
        int dash = Array.IndexOf(call.Arguments, "-c");
        Assert.Equal("sh", call.Arguments[dash - 1]);
        Assert.DoesNotContain(service.PackagePath, call.Arguments[dash + 1]); // the script never contains a path
        Assert.Equal(new[] { service.PackagePath, service.LogPath, service.ExitCodePath }, call.Arguments.Skip(dash + 3).ToArray());
    }

    [Fact]
    public async Task StatusFollowsTheExitCodeTheInstallerLeaves()
    {
        UpdateService service = Service();
        Assert.Equal("idle", (await service.StatusAsync()).State);

        await service.StageAsync(Deb());
        await service.StartAsync();
        runner.Respond = (program, _) => program == "systemctl" ? new CommandResult(0, "active\n", "") : new CommandResult(0, "", "");
        Assert.Equal("running", (await service.StatusAsync()).State);

        File.WriteAllText(service.LogPath, "Setting up lumenvision-backend\n");
        File.WriteAllText(service.ExitCodePath, "0\n");
        UpdateStatus done = await service.StatusAsync();
        Assert.Equal("succeeded", done.State);
        Assert.Contains("Setting up", done.Log);

        File.WriteAllText(service.ExitCodePath, "100\n");
        Assert.Equal("failed", (await service.StatusAsync()).State);
    }

    [Fact]
    public async Task AnInstallWhoseUnitDisappearedWithoutAnExitCodeCountsAsFailed()
    {
        UpdateService service = Service();
        await service.StageAsync(Deb());
        await service.StartAsync();
        runner.Respond = (program, _) => program == "systemctl" ? new CommandResult(3, "inactive\n", "") : new CommandResult(0, "", "");

        Assert.Equal("failed", (await service.StatusAsync()).State);
    }

    [Fact]
    public async Task ASecondInstallCannotStartWhileOneRuns()
    {
        UpdateService service = Service();
        await service.StageAsync(Deb());
        await service.StartAsync();
        runner.Respond = (program, _) => program == "systemctl" ? new CommandResult(0, "active\n", "") : new CommandResult(0, "", "");

        await Assert.ThrowsAsync<ApiException>(() => service.StartAsync());
    }

    [Fact]
    public void ControlFieldsAreReadByName()
    {
        var (package, version, architecture) = UpdateService.ParseControlFields("Package: p\nVersion: 2:1.0-3\nArchitecture: all\n");

        Assert.Equal(("p", "2:1.0-3", "all"), (package, version, architecture));
    }
}
