using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using Server;
using Server.Web;
using Xunit;

namespace Server.Tests.Edge;

// The offline updater while an install is running.
public class UpdateEdgeTests : IDisposable
{
    private readonly string directory = Path.Combine(Path.GetTempPath(), $"lumen-edge-f-update-{Guid.NewGuid():N}");
    private readonly FakeRunner runner = new();

    public UpdateEdgeTests()
    {
        // dpkg reads the package; the installer unit, once started, stays active (systemctl is-active succeeds)
        runner.Respond = (program, _) => program switch
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

    private static readonly byte[] DebBytes = Encoding.ASCII.GetBytes("!<arch>\ndebian-binary   0 0 0 100644 4 `\n2.0\n");

    // F10: StartAsync refuses a second install while one runs, but StageAsync does not: an upload during an install truncates
    // updates/lumenvision-update.deb (FileMode.Create) while apt-get is reading it, and a refused upload (not a .deb, wrong package, aborted
    // request) then deletes it outright (Discard) - the running install loses its package. A second browser tab, or re-uploading because the
    // progress looked stuck, is enough.
    [Fact]
    [Trait("Category", "Edge")]
    public async Task F_AnUploadWhileAnInstallRunsIsRefusedAndLeavesThePackageAlone()
    {
        var service = new UpdateService(() => runner, directory);
        await service.StageAsync(new MemoryStream(DebBytes));
        await service.StartAsync();
        Assert.Equal("running", (await service.StatusAsync()).State);

        Exception? upload = await Record.ExceptionAsync(() => service.StageAsync(new MemoryStream(Encoding.ASCII.GetBytes("MZ not a deb"))));

        Assert.True(File.Exists(service.PackagePath), "the package being installed was deleted");
        Assert.Equal(DebBytes, File.ReadAllBytes(service.PackagePath));
        Assert.Equal(400, Assert.IsType<ApiException>(upload).StatusCode);
    }
}
