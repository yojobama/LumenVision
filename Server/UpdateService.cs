using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Server.Web;

namespace Server
{
    public sealed record StagedPackage(string Package, string Version, string Architecture, long SizeBytes);

    // "idle" (nothing started), "running", "succeeded" or "failed"; Log is the tail of the installer's output
    public sealed record UpdateStatus(string State, string Log);

    // Installing a LumenVision .deb uploaded to the device (no internet needed). The package is checked before anything runs: it must be an ar archive,
    // named lumenvision-backend and built for this board's architecture. The install runs under its own systemd unit, because the package's
    // postinst restarts this server and an install that died with it would be left half done.
    public sealed class UpdateService
    {
        public static UpdateService Instance { get; } = new UpdateService(() => PrivilegedCommand.Runner, "updates");

        public const string PackageName = "lumenvision-backend";
        public const string UnitName = "lumenvision-update";
        private const long MaxPackageBytes = 1024L * 1024 * 1024;

        private readonly Func<IPrivilegedRunner> runner;
        private readonly string directory;

        public UpdateService(Func<IPrivilegedRunner> runner, string directory)
        {
            this.runner = runner;
            this.directory = Path.GetFullPath(directory);
        }

        public string PackagePath => Path.Combine(directory, "lumenvision-update.deb");
        public string LogPath => Path.Combine(directory, "update.log");
        public string ExitCodePath => Path.Combine(directory, "update.exit");

        // an .deb is an ar archive, which starts with this
        private static readonly byte[] ArMagic = System.Text.Encoding.ASCII.GetBytes("!<arch>\n");

        // Saves the uploaded package and checks it; a package that is not a LumenVision one for this board is deleted and refused.
        public async Task<StagedPackage> StageAsync(Stream upload)
        {
            Directory.CreateDirectory(directory);
            await using (var file = new FileStream(PackagePath, FileMode.Create, FileAccess.Write))
            {
                var buffer = new byte[81920];
                long total = 0;
                int read;
                while ((read = await upload.ReadAsync(buffer)) > 0)
                {
                    total += read;
                    if (total > MaxPackageBytes) { file.Close(); Discard(); throw ApiException.BadRequest("the package is larger than 1 GB"); }
                    await file.WriteAsync(buffer.AsMemory(0, read));
                }
            }

            try
            {
                return await InspectAsync();
            }
            catch
            {
                Discard();
                throw;
            }
        }

        private async Task<StagedPackage> InspectAsync()
        {
            var info = new FileInfo(PackagePath);
            var header = new byte[ArMagic.Length];
            await using (var file = info.OpenRead())
            {
                if (await file.ReadAsync(header) != header.Length || !header.SequenceEqual(ArMagic))
                    throw ApiException.BadRequest("this is not a .deb package");
            }

            CommandResult fields = await runner().RunAsync("dpkg-deb", new[] { "--field", PackagePath, "Package", "Version", "Architecture" });
            if (!fields.Ok) throw ApiException.BadRequest("dpkg could not read this package");
            (string package, string version, string architecture) = ParseControlFields(fields.StdOut);

            if (package != PackageName) throw ApiException.BadRequest($"this is the package '{package}', not {PackageName}");
            CommandResult board = await runner().RunAsync("dpkg", new[] { "--print-architecture" });
            if (board.Ok && board.StdOut.Trim() is { Length: > 0 } boardArchitecture && architecture != "all" && architecture != boardArchitecture)
                throw ApiException.BadRequest($"the package is built for {architecture}, this board is {boardArchitecture}");
            return new StagedPackage(package, version, architecture, info.Length);
        }

        public static (string Package, string Version, string Architecture) ParseControlFields(string output)
        {
            string Field(string name)
            {
                foreach (string line in output.Split('\n'))
                {
                    if (line.StartsWith(name + ":", StringComparison.Ordinal)) return line[(name.Length + 1)..].Trim();
                }
                return "";
            }
            return (Field("Package"), Field("Version"), Field("Architecture"));
        }

        // Starts installing the staged package in the background and returns at once.
        public async Task StartAsync()
        {
            if (!File.Exists(PackagePath)) throw ApiException.BadRequest("no package has been uploaded");
            if ((await StatusAsync()).State == "running") throw ApiException.BadRequest("an update is already running");

            File.Delete(ExitCodePath);
            File.WriteAllText(LogPath, "");
            // the paths travel as positional parameters, so nothing in them is interpreted by the shell
            CommandResult started = await runner().RunAsync("systemd-run", new[]
            {
                $"--unit={UnitName}", "--collect", "--setenv=DEBIAN_FRONTEND=noninteractive",
                "sh", "-c", "apt-get install -y --allow-downgrades \"$1\" > \"$2\" 2>&1; echo $? > \"$3\"", "sh", PackagePath, LogPath, ExitCodePath,
            });
            if (!started.Ok) throw new InvalidOperationException($"could not start the installer: {started.StdErr.Trim()}");
        }

        public async Task<UpdateStatus> StatusAsync()
        {
            string log = File.Exists(LogPath) ? Tail(LogPath, 60) : "";
            if (File.Exists(ExitCodePath))
            {
                bool ok = File.ReadAllText(ExitCodePath).Trim() == "0";
                return new UpdateStatus(ok ? "succeeded" : "failed", log);
            }
            if (!File.Exists(LogPath)) return new UpdateStatus("idle", "");

            // started but no exit code yet: running, unless the unit is gone (the board rebooted or the unit was killed)
            CommandResult unit = await runner().RunAsync("systemctl", new[] { "is-active", UnitName });
            return new UpdateStatus(unit.Ok ? "running" : "failed", log);
        }

        private void Discard()
        {
            try { if (File.Exists(PackagePath)) File.Delete(PackagePath); } catch (IOException) { /* replaced by the next upload */ }
        }

        private static string Tail(string path, int lines)
        {
            string[] all = File.ReadAllLines(path);
            return string.Join('\n', all.Length <= lines ? all : all[^lines..]);
        }
    }
}
