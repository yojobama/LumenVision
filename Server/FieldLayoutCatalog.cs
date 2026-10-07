using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Server
{
    // One AprilTag field layout a pipeline can use. Id is the file name without extension (bundled layouts live in fieldLayouts/bundled/).
    public sealed record FieldLayoutInfo(string Id, string Name, int TagCount);

    // The field layouts shipped with the server (copied from the repository's fieldLayouts/ folder by Server.csproj) and helpers for
    // WPILib-format layout files in general.
    public static class FieldLayoutCatalog
    {
        public static string BundledDirectory => Path.Combine(AppContext.BaseDirectory, "fieldLayouts", "bundled");

        // where a layout copied for one detector or pipeline is stored (the same folder uploads go to)
        public static string UserDirectory => Path.Combine(AppContext.BaseDirectory, "fieldLayouts");

        private static readonly Dictionary<string, string> DisplayNames = new(StringComparer.OrdinalIgnoreCase)
        {
            ["2026-rebuilt-welded"] = "2026 REBUILT (welded)",
            ["2026-rebuilt-andymark"] = "2026 REBUILT (AndyMark)",
            ["2025-reefscape-welded"] = "2025 Reefscape (welded)",
            ["2025-reefscape-andymark"] = "2025 Reefscape (AndyMark)",
            ["2024-crescendo"] = "2024 Crescendo",
            ["2023-chargedup"] = "2023 Charged Up",
            ["2022-rapidreact"] = "2022 Rapid React",
        };

        // newest season first
        public static List<FieldLayoutInfo> ListBundled()
        {
            if (!Directory.Exists(BundledDirectory)) return new List<FieldLayoutInfo>();
            return Directory.EnumerateFiles(BundledDirectory, "*.json")
                .Select(path => Path.GetFileNameWithoutExtension(path))
                .OrderByDescending(id => id, StringComparer.OrdinalIgnoreCase)
                .Select(id => new FieldLayoutInfo(id, DisplayNames.TryGetValue(id, out string? name) ? name : id, CountTags(PathOf(id))))
                .ToList();
        }

        // the file of a bundled layout, or null when the id names none (only letters, digits, '-' and '_' are accepted, so it cannot leave the folder)
        public static string? ResolveBundled(string id)
        {
            if (string.IsNullOrEmpty(id) || !id.All(c => char.IsLetterOrDigit(c) || c == '-' || c == '_')) return null;
            string path = PathOf(id);
            return File.Exists(path) ? path : null;
        }

        private static string PathOf(string id) => Path.Combine(BundledDirectory, id + ".json");

        // How many tags a WPILib-format layout file holds; -1 when the file is missing or is not a layout.
        public static int CountTags(string path)
        {
            try
            {
                using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(path));
                return doc.RootElement.TryGetProperty("tags", out JsonElement tags) && tags.ValueKind == JsonValueKind.Array ? tags.GetArrayLength() : -1;
            }
            catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException)
            {
                return -1;
            }
        }

        // Copies a bundled layout to `destination` so the detector or pipeline owns a private copy; returns its tag count.
        public static int CopyBundled(string id, string destination)
        {
            string source = ResolveBundled(id) ?? throw Server.Web.ApiException.NotFound($"no bundled field layout '{id}'");
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            File.Copy(source, destination, true);
            return CountTags(destination);
        }
    }
}
