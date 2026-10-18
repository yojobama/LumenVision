using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json.Nodes;

namespace Server
{
    // Rewrites a saved configuration that still has per-camera pipeline profiles: each profile becomes a detector node of its own bound to the
    // camera, the active profile's node stays running and the others are saved as disabled.
    public static class NodeMigration
    {
        public sealed record LayoutCopy(int NodeId, string FromPath);

        public sealed record Result(string Json, List<LayoutCopy> LayoutCopies, List<string> Notes);

        private static readonly string[] LegacySourceFields = { "Profiles", "ActiveProfileIndex", "ActiveDetectionSinkId" };

        public static bool Needed(string json)
        {
            if (JsonNode.Parse(json) is not JsonObject root || root["Sources"] is not JsonArray sources) return false;
            return sources.OfType<JsonObject>().Any(s => s["Profiles"] is JsonArray profiles && profiles.Count > 0);
        }

        public static Result Migrate(string json, Func<int> newId)
        {
            var layoutCopies = new List<LayoutCopy>();
            var notes = new List<string>();
            var root = (JsonObject)JsonNode.Parse(json)!;
            var sources = (JsonArray)(root["Sources"] ?? new JsonArray());
            var sinks = (JsonArray)(root["Sinks"] ?? new JsonArray());
            var disabled = new HashSet<int>((root["DisabledNodes"] as JsonArray)?.Select(n => n!.GetValue<int>()) ?? Enumerable.Empty<int>());
            var usedIds = new HashSet<int>(sinks.OfType<JsonObject>().Select(s => s["Id"]!.GetValue<int>())
                .Concat(sources.OfType<JsonObject>().Select(s => s["Id"]!.GetValue<int>())));

            foreach (JsonObject source in sources.OfType<JsonObject>().ToList())
            {
                if (source["Profiles"] is not JsonArray profiles || profiles.Count == 0)
                {
                    Strip(source);
                    continue;
                }

                int activeIndex = source["ActiveProfileIndex"]?.GetValue<int>() ?? -1;
                int? activeSinkId = source["ActiveDetectionSinkId"]?.GetValue<int>();
                string cameraName = source["Name"]?.GetValue<string>() ?? "camera";

                foreach (JsonObject profile in profiles.OfType<JsonObject>().OrderBy(p => p["Index"]!.GetValue<int>()))
                {
                    int index = profile["Index"]!.GetValue<int>();
                    string name = profile["Name"]?.GetValue<string>() ?? $"pipeline {index}";
                    bool isActive = index == activeIndex;

                    JsonObject? node = isActive && activeSinkId.HasValue
                        ? sinks.OfType<JsonObject>().FirstOrDefault(s => s["Id"]!.GetValue<int>() == activeSinkId.Value)
                        : null;
                    if (node == null)
                    {
                        int id;
                        do { id = newId(); } while (!usedIds.Add(id));
                        node = NewNode(id, $"{cameraName} - {name}", profile, source);
                        sinks.Add(node);
                    }

                    int nodeId = node["Id"]!.GetValue<int>();
                    if (!isActive || profile["DriverMode"]?.GetValue<bool>() == true) disabled.Add(nodeId);
                    else disabled.Remove(nodeId);

                    if (profile["FieldLayoutPath"]?.GetValue<string>() is { Length: > 0 } layout)
                        layoutCopies.Add(new LayoutCopy(nodeId, layout));

                    if (isActive) FoldOverrides(source, profile["CameraOverrides"] as JsonObject, cameraName);
                    else if (profile["CameraOverrides"] != null)
                        notes.Add($"camera {cameraName}: pipeline '{name}' had its own camera settings, which are not kept");
                }

                Strip(source);
            }

            // each sink carries a copy of its source, so those copies lose the legacy fields too
            foreach (JsonObject sink in sinks.OfType<JsonObject>())
            {
                if (sink["Source"] is JsonObject embedded) Strip(embedded);
                if (sink["Source2"] is JsonObject embedded2) Strip(embedded2);
            }

            root["DisabledNodes"] = new JsonArray(disabled.OrderBy(i => i).Select(i => (JsonNode)JsonValue.Create(i)!).ToArray());
            return new Result(root.ToJsonString(new System.Text.Json.JsonSerializerOptions { WriteIndented = true }), layoutCopies, notes);
        }

        private static JsonObject NewNode(int id, string name, JsonObject profile, JsonObject source)
        {
            int kind = profile["Kind"]?.GetValue<int>() ?? 0;
            var node = new JsonObject
            {
                ["Type"] = kind,
                ["Id"] = id,
                ["Name"] = name,
                ["Source"] = JsonNode.Parse(source.ToJsonString()),
            };
            if (kind == 0)
            {
                Copy(profile, "TagSize", node, "ApriltagTagSize");
                Copy(profile, "Backend", node, "ApriltagBackend");
                Copy(profile, "Threads", node, "ApriltagThreads");
                Copy(profile, "QuadDecimate", node, "ApriltagQuadDecimate");
                Copy(profile, "RefineEdges", node, "ApriltagRefineEdges");
                Copy(profile, "RefineMode", node, "ApriltagRefineMode");
                Copy(profile, "Advanced", node, "ApriltagAdvanced");
            }
            else
            {
                Copy(profile, "ModelId", node, "ObjectDetectionModelId");
                Copy(profile, "ConfThreshold", node, "ObjectDetectionConfThreshold");
                Copy(profile, "NmsThreshold", node, "ObjectDetectionNmsThreshold");
            }
            return node;
        }

        private static void Copy(JsonObject from, string fromKey, JsonObject to, string toKey)
        {
            if (from[fromKey] is JsonNode value) to[toKey] = JsonNode.Parse(value.ToJsonString());
        }

        // the active pipeline's camera settings become the camera's own
        private static void FoldOverrides(JsonObject source, JsonObject? overrides, string cameraName)
        {
            if (overrides == null) return;
            if (overrides["ControlValues"] is JsonObject values && values.Count > 0)
            {
                JsonObject own = source["ControlValues"] as JsonObject ?? new JsonObject();
                foreach (var pair in values) own[pair.Key] = pair.Value == null ? null : JsonNode.Parse(pair.Value.ToJsonString());
                source["ControlValues"] = own;
            }
            if (overrides["Transform"] is JsonNode transform) source["Transform"] = JsonNode.Parse(transform.ToJsonString());
            if (overrides["FpsLimit"] is JsonNode fps) source["FpsLimit"] = JsonNode.Parse(fps.ToJsonString());
        }

        private static void Strip(JsonObject source)
        {
            foreach (string field in LegacySourceFields) source.Remove(field);
        }
    }
}
