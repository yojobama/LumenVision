using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace Server
{
    // What this device is running; shown in the UI and written into settings exports.
    public sealed record VersionInfo(string Server, string LumenCore, string Os, string Kernel, string Architecture, string Runtime, string Hostname)
    {
        public static VersionInfo Current()
        {
            // "1.2.3+commit" -> "1.2.3": the build metadata after '+' is the commit hash dotnet appends
            string server = Assembly.GetExecutingAssembly().GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? "unknown";
            int plus = server.IndexOf('+');
            if (plus > 0) server = server[..plus];

            string lumenCore;
            try
            {
                lumenCore = ManagerWrapper.Instance.GetLumenCoreVersion();
            }
            catch (Exception)
            {
                lumenCore = "unknown";
            }
            return new VersionInfo(server, lumenCore, OsName(), RuntimeInformation.OSDescription, RuntimeInformation.OSArchitecture.ToString(),
                RuntimeInformation.FrameworkDescription, Environment.MachineName);
        }

        // "Armbian 25.2 trixie" from /etc/os-release; the generic description elsewhere
        private static string OsName()
        {
            try
            {
                if (File.Exists("/etc/os-release"))
                {
                    foreach (string line in File.ReadAllLines("/etc/os-release"))
                    {
                        if (line.StartsWith("PRETTY_NAME=", StringComparison.Ordinal)) return line["PRETTY_NAME=".Length..].Trim('"');
                    }
                }
            }
            catch (IOException) { /* falls through */ }
            return RuntimeInformation.OSDescription;
        }
    }
}
