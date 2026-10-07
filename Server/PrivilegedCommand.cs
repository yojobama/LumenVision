using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Server
{
    public sealed record CommandResult(int ExitCode, string StdOut, string StdErr)
    {
        public bool Ok => ExitCode == 0;
    }

    // Runs a program with root privileges. Callers pass the program and its arguments as separate items, never a shell string, so nothing a
    // client sent can be interpreted by a shell.
    public interface IPrivilegedRunner
    {
        Task<CommandResult> RunAsync(string program, IReadOnlyList<string> arguments, TimeSpan? timeout = null, CancellationToken cancellationToken = default);
    }

    // `sudo -n`: the lumen user has passwordless sudo (scripts/deb/postinst writes /etc/sudoers.d/lumenvision), so a command that would need a
    // password fails at once instead of waiting for input. Every call is logged.
    public sealed class SudoRunner : IPrivilegedRunner
    {
        private static readonly TimeSpan DefaultTimeout = TimeSpan.FromSeconds(60);

        public static ProcessStartInfo BuildStartInfo(string program, IReadOnlyList<string> arguments)
        {
            var info = new ProcessStartInfo("sudo")
            {
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                RedirectStandardInput = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            };
            info.ArgumentList.Add("-n");
            info.ArgumentList.Add("--");
            info.ArgumentList.Add(program);
            foreach (string argument in arguments) info.ArgumentList.Add(argument);
            return info;
        }

        public async Task<CommandResult> RunAsync(string program, IReadOnlyList<string> arguments, TimeSpan? timeout = null, CancellationToken cancellationToken = default)
        {
            if (!RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
                throw new PlatformNotSupportedException("privileged device operations are only available on the coprocessor (Linux)");

            Console.WriteLine($"[privileged] sudo {program} {string.Join(' ', arguments)}");
            using var process = new Process { StartInfo = BuildStartInfo(program, arguments) };
            var stdout = new StringBuilder();
            var stderr = new StringBuilder();
            process.OutputDataReceived += (_, e) => { if (e.Data != null) stdout.AppendLine(e.Data); };
            process.ErrorDataReceived += (_, e) => { if (e.Data != null) stderr.AppendLine(e.Data); };

            process.Start();
            process.StandardInput.Close();
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();

            using var limit = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            limit.CancelAfter(timeout ?? DefaultTimeout);
            try
            {
                await process.WaitForExitAsync(limit.Token);
            }
            catch (OperationCanceledException)
            {
                try { process.Kill(entireProcessTree: true); } catch (InvalidOperationException) { /* already exited */ }
                throw new TimeoutException($"{program} did not finish within {(timeout ?? DefaultTimeout).TotalSeconds:0} seconds");
            }
            process.WaitForExit(); // flushes the asynchronous output readers
            var result = new CommandResult(process.ExitCode, stdout.ToString(), stderr.ToString());
            if (!result.Ok) Console.WriteLine($"[privileged] {program} exited {result.ExitCode}: {result.StdErr.Trim()}");
            return result;
        }
    }

    // The runner device operations use; tests swap in a fake.
    public static class PrivilegedCommand
    {
        public static IPrivilegedRunner Runner { get; set; } = new SudoRunner();

        public static Task<CommandResult> RunAsync(string program, params string[] arguments) => Runner.RunAsync(program, arguments);
    }
}
