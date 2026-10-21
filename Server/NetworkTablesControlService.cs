using Microsoft.Extensions.Hosting;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace Server
{
    // Applies robot-written NetworkTables config each tick, for every NetworkTablesSink:
    //  <root>/config/recording                 (bool)  -> SinkManager.SetAllRecording
    //  <root>/config/ledMode                   (int)   -> LedController (-1 auto, 0 off, 1 on, 2 blink)
    //  <root>/<node>/config/constrainedSeed  (double[3]: x, y, yaw) and config/robotToCamera (double[7]: x, y, z, qw, qx, qy, qz)
    //                                          -> the detector's floor-constrained solve (published in the result packet)
    //  <root>/<node>/config/enabled            (bool)  -> start or stop that node (SinkManager.SetSinkEnabled)
    //  <root>/<node>/config/snapshot           (any write) -> save that node's latest frame
    //  <root>/<node>/config/recording          (bool)  -> record that node (SinkManager.SetNodeRecording)
    //  <root>/<node>/config/fpsLimit           (int)   -> cap how many results per second that node publishes (<= 0: unlimited)
    // and publishes the settings actually in effect back under status/ (recording, ledMode, per node enabled, recording
    // and fpsLimit), plus the graph description under .graph. <node> is the node's table (see .graph). Requests are idempotent desired state,
    // so several NT sinks seeing the same write is harmless.
    public sealed class NetworkTablesControlService : BackgroundService
    {
        // fast enough for recording changes at the auto/teleop boundary to land within a frame or two; in-process calls only
        private static readonly TimeSpan TickInterval = TimeSpan.FromMilliseconds(100);
        // status read-backs are re-published this often even when unchanged, so they survive an NT sink being recreated
        private static readonly TimeSpan StatusRefresh = TimeSpan.FromSeconds(2);

        // the seed and the camera mount arrive as separate NT values, so each is remembered until both are known
        private static readonly Dictionary<int, double[]> ConstrainedSeeds = new();
        private static readonly Dictionary<int, double[]> ConstrainedMounts = new();

        // NT sinks that were rebuilt under the same id: the new native sink has none of the aliases or status values the caches below say were applied
        private static readonly System.Collections.Concurrent.ConcurrentQueue<int> ForgottenSinks = new();

        // Call after a NetworkTablesSink was rebuilt in place: the next tick applies its camera aliases and republishes its status from scratch.
        public static void ForgetSink(int ntSinkId) => ForgottenSinks.Enqueue(ntSinkId);

        internal static void ApplyForgottenSinks()
        {
            while (ForgottenSinks.TryDequeue(out int ntSinkId))
            {
                foreach (var key in AppliedAliases.Keys.Where(k => k.ntSinkId == ntSinkId).ToList()) AppliedAliases.Remove(key);
                foreach (var key in PublishedStatus.Keys.Where(k => k.ntSinkId == ntSinkId).ToList()) PublishedStatus.Remove(key);
                PublishedGraph.Remove(ntSinkId);
            }
        }

        internal static bool HasAppliedAlias(int ntSinkId, int nodeId) => AppliedAliases.ContainsKey((ntSinkId, nodeId));
        internal static void RecordAppliedAliasForTest(int ntSinkId, int nodeId, string name) => AppliedAliases[(ntSinkId, nodeId)] = name;

        private static readonly Dictionary<(int ntSinkId, int nodeId), string> AppliedAliases = new();
        private static readonly Dictionary<(int ntSinkId, int nodeId), (string status, DateTime at)> PublishedStatus = new();

        protected override async Task ExecuteAsync(CancellationToken stoppingToken)
        {
            while (!stoppingToken.IsCancellationRequested)
            {
                try
                {
                    Tick();
                }
                catch (Exception ex)
                {
                    // one bad request must not stop robot control for the rest of the match
                    Console.WriteLine($"NetworkTablesControlService tick failed: {ex.Message}");
                }

                try
                {
                    await Task.Delay(TickInterval, stoppingToken);
                }
                catch (TaskCanceledException)
                {
                    break;
                }
            }
        }

        private static void Tick()
        {
            ApplyForgottenSinks();
            List<int> detectorIds = SinkManager.Instance.GetAllSinks()
                .Where(s => s.Type == SinkType.ApriltagSink || s.Type == SinkType.ObjectDetectionSink).Select(s => s.Id).ToList();
            // the vision LED follows the detectors whether or not a robot is connected
            LedController.Instance.SetAutoActive(detectorIds.Any(SinkManager.Instance.IsSinkRunning));
            List<int> ntSinkIds = SinkManager.Instance.getAllSinkIds()
                .Where(id => SinkManager.Instance.GetSinkById(id)?.Type == SinkType.NetworkTablesSink)
                .ToList();
            if (ntSinkIds.Count == 0) return;

            Dictionary<int, string> tables = ComputeNodeTables(
                SourceManager.Instance.GetAllSourceIds().Select(id => SourceManager.Instance.GetSourceById(id)).Where(s => s != null)!,
                SinkManager.Instance.GetAllSinks());
            RefreshAliases(ntSinkIds, tables);

            // every NetworkTablesSink publishes every detector node, however it was bound
            foreach (int ntSinkId in ntSinkIds)
                foreach (int detectorId in detectorIds)
                    ManagerWrapper.Instance.EnsureSourceBoundToSink(detectorId, ntSinkId);

            // the latest recording/LED request across all NT sinks wins (they all see the same topics)
            int recordingRequest = -1;
            int ledRequest = -2;
            foreach (int ntSinkId in ntSinkIds)
            {
                int request = ManagerWrapper.Instance.PollNetworkTablesSinkRecordingRequest(ntSinkId);
                if (request != -1) recordingRequest = request;
                int led = ManagerWrapper.Instance.PollNetworkTablesSinkLedRequest(ntSinkId);
                if (led != -2) ledRequest = led;
                ApplyConfigRequests(ManagerWrapper.Instance.PollNetworkTablesSinkConfigRequests(ntSinkId));
            }

            if (recordingRequest != -1)
            {
                bool start = recordingRequest == 1;
                int running = SinkManager.Instance.SetAllRecording(start);
                Console.WriteLine(start
                    ? $"Robot requested recording over NT - {running} RecordSink(s) running"
                    : "Robot requested recording stop over NT");
            }

            if (ledRequest != -2) ApplyLedRequest(ledRequest);

            bool recording = SinkManager.Instance.IsAnyRecording();
            foreach (int ntSinkId in ntSinkIds)
            {
                ManagerWrapper.Instance.SetNetworkTablesSinkRecordingStatus(ntSinkId, recording);
                ManagerWrapper.Instance.SetNetworkTablesSinkLedStatus(ntSinkId, (int)LedController.Instance.Mode);
            }
            PublishCameraStatus(ntSinkIds, detectorIds, SourceManager.Instance.GetAllSourceIds());
            PublishGraph(ntSinkIds, tables);
        }

        private static readonly Dictionary<int, (string json, DateTime at)> PublishedGraph = new();

        // every node (sources and sinks) with its display name, the NT table its topics are under (see ComputeNodeTables; the id when it has none),
        // kind, running state and input
        internal static string BuildGraphJson(IEnumerable<Source> sources, IEnumerable<Sink> sinks, Dictionary<int, string> tables, Func<int, bool> isRunning, Func<int, bool>? isRecording = null)
        {
            var nodes = new List<object>();
            foreach (Source source in sources)
                nodes.Add(new { id = source.Id, name = source.Name, table = tables.TryGetValue(source.Id, out string? sourceTable) ? sourceTable : source.Id.ToString(), kind = source.Type.ToString(), enabled = true, recording = isRecording?.Invoke(source.Id) ?? false, input = (int?)source.CropParentId });
            foreach (Sink sink in sinks)
                nodes.Add(new
                {
                    id = sink.Id,
                    name = sink.Name,
                    table = tables.TryGetValue(sink.Id, out string? sinkTable) ? sinkTable : sink.Id.ToString(),
                    kind = sink.Type.ToString(),
                    enabled = isRunning(sink.Id),
                    recording = isRecording?.Invoke(sink.Id) ?? false,
                    input = (int?)sink.Source?.Id,
                });
            return JsonSerializer.Serialize(new { nodes });
        }

        private static void PublishGraph(List<int> ntSinkIds, Dictionary<int, string> tables)
        {
            var sources = SourceManager.Instance.GetAllSourceIds().Select(id => SourceManager.Instance.GetSourceById(id)).Where(s => s != null).Select(s => s!).ToList();
            var sinks = SinkManager.Instance.GetAllSinks().ToList();
            string json = BuildGraphJson(sources, sinks, tables, SinkManager.Instance.IsSinkRunning, SinkManager.Instance.IsNodeRecording);

            foreach (int ntSinkId in ntSinkIds)
            {
                if (PublishedGraph.TryGetValue(ntSinkId, out var last) && last.json == json && DateTime.UtcNow - last.at < StatusRefresh) continue;
                ManagerWrapper.Instance.SetNetworkTablesSinkGraph(ntSinkId, json);
                PublishedGraph[ntSinkId] = (json, DateTime.UtcNow);
            }
        }

        // node id -> the NT table its topics are published under: the node's own name with characters outside [A-Za-z0-9_.-] replaced
        // by '_', made unique across cameras and detectors by appending the id; a node without a name gets its id. Other sinks have no table.
        internal static Dictionary<int, string> ComputeNodeTables(IEnumerable<Source> sources, IEnumerable<Sink> sinks)
        {
            var tables = new Dictionary<int, string>();
            var used = new HashSet<string>();
            var named = sources.Select(s => (s.Id, s.Name))
                .Concat(sinks.Where(s => s.Type == SinkType.ApriltagSink || s.Type == SinkType.ObjectDetectionSink).Select(s => (s.Id, s.Name)))
                .OrderBy(n => n.Id);
            foreach (var (id, name) in named)
            {
                string table = new string((name ?? string.Empty).Select(c => char.IsAsciiLetterOrDigit(c) || c == '_' || c == '.' || c == '-' ? c : '_').ToArray());
                if (table.Length == 0) table = id.ToString();
                else if (!used.Add(table)) table = $"{table}-{id}";
                used.Add(table);
                tables[id] = table;
            }
            return tables;
        }

        private static void RefreshAliases(List<int> ntSinkIds, Dictionary<int, string> tables)
        {
            foreach (int ntSinkId in ntSinkIds)
            {
                foreach (var (nodeId, name) in tables)
                {
                    var key = (ntSinkId, nodeId);
                    if (AppliedAliases.TryGetValue(key, out string? applied) && applied == name) continue;
                    ManagerWrapper.Instance.SetNetworkTablesSinkNodeAlias(ntSinkId, nodeId.ToString(), name);
                    AppliedAliases[key] = name;
                }

                foreach (var key in AppliedAliases.Keys.Where(k => k.ntSinkId == ntSinkId && !tables.ContainsKey(k.nodeId)).ToList())
                {
                    ManagerWrapper.Instance.SetNetworkTablesSinkNodeAlias(ntSinkId, key.nodeId.ToString(), "");
                    AppliedAliases.Remove(key);
                }
            }
        }

        private static void PublishCameraStatus(List<int> ntSinkIds, List<int> detectorIds, IEnumerable<int> sourceIds)
        {
            foreach (int nodeId in sourceIds)
            {
                bool recording = SinkManager.Instance.IsNodeRecording(nodeId);
                foreach (int ntSinkId in ntSinkIds)
                {
                    var key = (ntSinkId, nodeId);
                    string sourceStatus = $"recording|{recording}";
                    if (PublishedStatus.TryGetValue(key, out var last) && last.status == sourceStatus && DateTime.UtcNow - last.at < StatusRefresh) continue;
                    ManagerWrapper.Instance.SetNetworkTablesSinkNodeRecording(ntSinkId, nodeId.ToString(), recording);
                    PublishedStatus[key] = (sourceStatus, DateTime.UtcNow);
                }
            }

            foreach (int nodeId in detectorIds)
            {
                int fpsLimit = ManagerWrapper.Instance.GetSourceFpsLimit(nodeId);
                bool enabled = SinkManager.Instance.IsSinkRunning(nodeId);
                bool nodeRecording = SinkManager.Instance.IsNodeRecording(nodeId);
                string status = $"{fpsLimit}|{enabled}|{nodeRecording}";

                foreach (int ntSinkId in ntSinkIds)
                {
                    var key = (ntSinkId, nodeId);
                    if (PublishedStatus.TryGetValue(key, out var last) && last.status == status && DateTime.UtcNow - last.at < StatusRefresh)
                        continue;
                    ManagerWrapper.Instance.SetNetworkTablesSinkNodeStatus(ntSinkId, nodeId.ToString(), fpsLimit);
                    ManagerWrapper.Instance.SetNetworkTablesSinkNodeEnabled(ntSinkId, nodeId.ToString(), enabled);
                    ManagerWrapper.Instance.SetNetworkTablesSinkNodeRecording(ntSinkId, nodeId.ToString(), nodeRecording);
                    PublishedStatus[key] = (status, DateTime.UtcNow);
                }
            }
        }

        private static void ApplyLedRequest(int value)
        {
            if (value < (int)LedMode.Auto || value > (int)LedMode.Blink)
            {
                Console.WriteLine($"NT ledMode request {value} ignored: expected -1 (auto), 0 (off), 1 (on) or 2 (blink)");
                return;
            }
            LedController.Instance.SetMode((LedMode)value);
        }

        // entries are {"sourceId": "<nodeId>", "enabled"?, "recording"?, "snapshots"?, "fpsLimit"?}
        // (see NetworkTablesSink::PollConfigRequests); each applies to that node.
        private static void ApplyConfigRequests(string json)
        {
            if (string.IsNullOrEmpty(json) || json == "[]") return;
            using JsonDocument doc = JsonDocument.Parse(json);
            foreach (JsonElement entry in doc.RootElement.EnumerateArray())
            {
                if (!int.TryParse(entry.GetProperty("sourceId").GetString(), out int nodeId)) continue;

                if (entry.TryGetProperty("constrainedSeed", out JsonElement seedElement))
                    ConstrainedSeeds[nodeId] = seedElement.EnumerateArray().Select(v => v.GetDouble()).ToArray();
                if (entry.TryGetProperty("robotToCamera", out JsonElement mountElement))
                    ConstrainedMounts[nodeId] = mountElement.EnumerateArray().Select(v => v.GetDouble()).ToArray();
                if ((seedElement.ValueKind != JsonValueKind.Undefined || mountElement.ValueKind != JsonValueKind.Undefined)
                    && ConstrainedSeeds.TryGetValue(nodeId, out double[]? seed) && ConstrainedMounts.TryGetValue(nodeId, out double[]? mount))
                {
                    try
                    {
                        var mountVector = new VectorDouble();
                        foreach (double value in mount) mountVector.Add(value);
                        ManagerWrapper.Instance.SetApriltagConstrainedSeed(nodeId, seed[0], seed[1], seed[2], mountVector);
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"NT constrained seed for {nodeId} ignored: {ex.Message}");
                    }
                }

                if (entry.TryGetProperty("enabled", out JsonElement enabledElement))
                {
                    try
                    {
                        SinkManager.Instance.SetSinkEnabled(nodeId, enabledElement.GetBoolean());
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"NT enabled request for {nodeId} ignored: {ex.Message}");
                    }
                }

                if (entry.TryGetProperty("recording", out JsonElement recordingElement))
                {
                    try
                    {
                        SinkManager.Instance.SetNodeRecording(nodeId, recordingElement.GetBoolean());
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"NT recording request for {nodeId} ignored: {ex.Message}");
                    }
                }

                if (entry.TryGetProperty("snapshots", out _)) TakeNodeSnapshot(nodeId);

                if (entry.TryGetProperty("fpsLimit", out JsonElement fpsLimit))
                {
                    try
                    {
                        int fps = fpsLimit.GetInt32();
                        // a camera remembers its cap across restarts; a detector's applies until it is rebuilt
                        if (SourceManager.Instance.GetSourceById(nodeId) != null) SourceManager.Instance.SetFpsLimit(nodeId, fps);
                        else ManagerWrapper.Instance.SetSourceFpsLimit(nodeId, fps > 0 ? fps : -1);
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"NT fpsLimit request for node {nodeId} ignored: {ex.Message}");
                    }
                }
            }
        }

        // a snapshot waits for a fresh frame, so it runs off the tick
        private static void TakeNodeSnapshot(int nodeId)
        {
            _ = Task.Run(() =>
            {
                try
                {
                    string? saved = SnapshotService.Instance.SaveNode(nodeId);
                    Console.WriteLine(saved == null ? $"NT snapshot request for node {nodeId}: no frame available" : $"NT snapshot saved: {saved}");
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"NT snapshot request for node {nodeId} failed: {ex.Message}");
                }
            });
        }

    }
}
