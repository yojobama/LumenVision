using System;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Server
{
    // The GPIO line that drives a vision LED ring; disabled until configured, so machines without GPIO need no setup.
    public sealed class LedSettings
    {
        public bool Enabled { get; set; }
        // gpiochip number and line offset of the LED's GPIO
        public int Chip { get; set; }
        public int Line { get; set; }
        public bool ActiveLow { get; set; }
    }

    // How the coprocessor reaches the robot's NetworkTables server; every NetworkTablesSink uses it.
    public sealed class NetworkTablesSettings
    {
        // "team" resolves the roboRIO from the team number (10.TE.AM.2); "server" connects to an explicit address (bench testing)
        public string Mode { get; set; } = "team";
        public int? TeamNumber { get; set; }
        public string? ServerAddress { get; set; }
        // 0 = NT4's default port
        public int Port { get; set; }
        public string RootTable { get; set; } = "lumenvision";
        public string ClientIdentity { get; set; } = "lumenvision";
    }

    public sealed class DeviceSettingsData
    {
        public LedSettings Led { get; set; } = new LedSettings();
        public NetworkTablesSettings NetworkTables { get; set; } = new NetworkTablesSettings();
    }

    // Device-level settings persisted to settings.json next to the server.
    public sealed class DeviceSettings
    {
        public static DeviceSettings Instance { get; } = new DeviceSettings("settings.json");

        private readonly string path;
        private readonly object sync = new object();
        private DeviceSettingsData data = new DeviceSettingsData();

        public DeviceSettings(string path)
        {
            this.path = path;
            Load();
        }

        public DeviceSettingsData Data
        {
            get { lock (sync) return data; }
        }

        private void Load()
        {
            if (!File.Exists(path)) return;
            try
            {
                data = JsonSerializer.Deserialize<DeviceSettingsData>(File.ReadAllText(path)) ?? new DeviceSettingsData();
            }
            catch (Exception)
            {
                // a corrupt file starts from defaults rather than keeping the server from booting
                data = new DeviceSettingsData();
            }
        }

        // The problem with these settings as a user-facing message, or null when they are usable. Rejected settings are never stored.
        public static string? Validate(DeviceSettingsData candidate)
        {
            NetworkTablesSettings nt = candidate.NetworkTables ?? new NetworkTablesSettings();
            if (nt.Mode != "team" && nt.Mode != "server") return "the NetworkTables mode must be 'team' or 'server'";
            if (nt.Mode == "team" && nt.TeamNumber is not (>= 1 and <= 25599)) return "a team number from 1 to 25599 is required";
            if (nt.Mode == "server" && string.IsNullOrWhiteSpace(nt.ServerAddress)) return "a NetworkTables server address is required";
            if (nt.Port < 0 || nt.Port > 65535) return "the port must be 0 to 65535";
            if (!IsTopicName(nt.RootTable)) return "the root table may only contain letters, digits, '-', '_' and '.'";
            if (!IsTopicName(nt.ClientIdentity)) return "the client identity may only contain letters, digits, '-', '_' and '.'";
            LedSettings led = candidate.Led ?? new LedSettings();
            if (led.Chip < 0 || led.Line < 0) return "the LED chip and line must not be negative";
            return null;
        }

        private static bool IsTopicName(string? value) =>
            !string.IsNullOrEmpty(value) && value.Length <= 64 && value.All(c => char.IsLetterOrDigit(c) || c == '-' || c == '_' || c == '.');

        // Replaces the settings (after Validate) and saves them.
        public void Replace(DeviceSettingsData next)
        {
            lock (sync) data = next;
            Save();
        }

        public void Save()
        {
            lock (sync)
            {
                File.WriteAllText(path, JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true }));
            }
        }
    }
}
