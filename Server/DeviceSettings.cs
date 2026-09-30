using System;
using System.IO;
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

    public sealed class DeviceSettingsData
    {
        public LedSettings Led { get; set; } = new LedSettings();
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

        public void Save()
        {
            lock (sync)
            {
                File.WriteAllText(path, JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true }));
            }
        }
    }
}
