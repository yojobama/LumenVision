using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Server.Web;

namespace Server
{
    // Restarting the server, rebooting or powering off the board, and wiping the saved data. Each takes effect a moment after the request returns,
    // so the browser gets its answer first.
    public sealed class PowerService
    {
        public static PowerService Instance { get; } = new PowerService(() => PrivilegedCommand.Runner, DeviceData.Default, TimeSpan.FromSeconds(1), () => DB.Instance.SuspendSaves());

        public const string ServiceName = "lumenvision.service";
        // what the user must type to confirm a factory reset
        public const string FactoryResetPhrase = "factory reset";

        private readonly Func<IPrivilegedRunner> runner;
        private readonly DeviceData data;
        private readonly TimeSpan delay;
        private readonly Action suspendSaves;

        public PowerService(Func<IPrivilegedRunner> runner, DeviceData data, TimeSpan delay, Action suspendSaves)
        {
            this.suspendSaves = suspendSaves;
            this.runner = runner;
            this.data = data;
            this.delay = delay;
        }

        private void Later(string program, params string[] arguments)
        {
            _ = Task.Run(async () =>
            {
                await Task.Delay(delay);
                try
                {
                    CommandResult result = await runner().RunAsync(program, arguments);
                    if (!result.Ok) Console.WriteLine($"[power] {program} {string.Join(' ', arguments)} failed: {result.StdErr.Trim()}");
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[power] {program} {string.Join(' ', arguments)} failed: {ex.Message}");
                }
            });
        }

        public void RestartServer() => Later("systemctl", "restart", ServiceName);

        public void Reboot() => Later("systemctl", "reboot");

        public void PowerOff() => Later("systemctl", "poweroff");

        // Deletes the saved configuration (the graph, settings, saved graph profiles and uploaded field layouts) and, unless kept, the calibrations,
        // models and media, then restarts the server so it starts empty. Returns how many files were deleted.
        public int FactoryReset(string confirmation, bool keepCalibrations, bool keepModels, bool keepMedia)
        {
            if (!string.Equals(confirmation?.Trim(), FactoryResetPhrase, StringComparison.OrdinalIgnoreCase))
                throw ApiException.BadRequest($"type \"{FactoryResetPhrase}\" to confirm");

            var groups = new List<DataGroup> { DataGroup.Configuration };
            if (!keepCalibrations) groups.Add(DataGroup.Calibrations);
            if (!keepModels) groups.Add(DataGroup.Models);
            if (!keepMedia) groups.Add(DataGroup.Media);

            // stop the running server from writing its in-memory state back over the files that are about to go
            suspendSaves();
            int deleted = data.Delete(groups);
            Console.WriteLine($"[power] factory reset deleted {deleted} files; restarting");
            RestartServer();
            return deleted;
        }
    }
}
