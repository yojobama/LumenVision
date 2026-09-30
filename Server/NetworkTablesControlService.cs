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
    //  <root>/config/ledMode                   (int)   -> LedController (-1 default, 0 off, 1 on, 2 blink)
    //  <root>/<camera>/config/driverMode       (bool)  -> SetDriverMode on that camera's detector
    //  <root>/<camera>/config/pipelineIndex    (int)   -> activate that camera's pipeline profile
    //  <root>/<camera>/config/fpsLimit         (int)   -> cap that camera's published frames per second (<= 0: unlimited)
    //  <root>/<camera>/config/inputSnapshot    (any write) -> save a raw-frame snapshot
    //  <root>/<camera>/config/outputSnapshot   (any write) -> save the detector's annotated frame
    // and publishes the settings actually in effect back under status/ (recording, ledMode, and per camera pipelineIndex,
    // driverMode, fpsLimit). <camera> is the camera's name (a detector's id also works). Requests are idempotent desired state,
    // so several NT sinks seeing the same write is harmless.
    public sealed class NetworkTablesControlService : BackgroundService
    {
        // fast enough for recording changes at the auto/teleop boundary to land within a frame or two; in-process calls only
        private static readonly TimeSpan TickInterval = TimeSpan.FromMilliseconds(100);
        // status read-backs are re-published this often even when unchanged, so they survive an NT sink being recreated
        private static readonly TimeSpan StatusRefresh = TimeSpan.FromSeconds(2);

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
            List<int> ntSinkIds = SinkManager.Instance.getAllSinkIds()
                .Where(id => SinkManager.Instance.GetSinkById(id)?.Type == SinkType.NetworkTablesSink)
                .ToList();
            if (ntSinkIds.Count == 0) return;

            Dictionary<int, string> aliases = ComputeCameraAliases(
                SourceManager.Instance.GetAllSourceIds().Select(id => SourceManager.Instance.GetSourceById(id)).Where(s => s != null)!,
                SinkManager.Instance.GetAllSinks());
            RefreshAliases(ntSinkIds, aliases);

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
            PublishCameraStatus(ntSinkIds, aliases);
        }

        // detector node id -> the name of the camera it runs on. A camera's active-profile detector is preferred; a standalone
        // detector bound straight to a camera is named after it too, unless another detector already holds that name.
        internal static Dictionary<int, string> ComputeCameraAliases(IEnumerable<Source> sources, IEnumerable<Sink> sinks)
        {
            var aliases = new Dictionary<int, string>();
            var usedNames = new HashSet<string>();
            List<Sink> sinkList = sinks.ToList();

            foreach (Source camera in sources.Where(s => s.Type == SourceType.Camera))
            {
                if (camera.ActiveDetectionSinkId is int nodeId && sinkList.Any(s => s.Id == nodeId) && usedNames.Add(camera.Name))
                    aliases[nodeId] = camera.Name;
            }

            foreach (Sink sink in sinkList.Where(s => s.Type == SinkType.ApriltagSink || s.Type == SinkType.ObjectDetectionSink))
            {
                if (aliases.ContainsKey(sink.Id)) continue;
                Source? bound = sink.Source;
                if (bound != null && bound.Type == SourceType.Camera && usedNames.Add(bound.Name))
                    aliases[sink.Id] = bound.Name;
            }
            return aliases;
        }

        private static void RefreshAliases(List<int> ntSinkIds, Dictionary<int, string> aliases)
        {
            foreach (int ntSinkId in ntSinkIds)
            {
                foreach (var (nodeId, name) in aliases)
                {
                    var key = (ntSinkId, nodeId);
                    if (AppliedAliases.TryGetValue(key, out string? applied) && applied == name) continue;
                    ManagerWrapper.Instance.SetNetworkTablesSinkNodeAlias(ntSinkId, nodeId.ToString(), name);
                    AppliedAliases[key] = name;
                }

                foreach (var key in AppliedAliases.Keys.Where(k => k.ntSinkId == ntSinkId && !aliases.ContainsKey(k.nodeId)).ToList())
                {
                    ManagerWrapper.Instance.SetNetworkTablesSinkNodeAlias(ntSinkId, key.nodeId.ToString(), "");
                    AppliedAliases.Remove(key);
                }
            }
        }

        private static void PublishCameraStatus(List<int> ntSinkIds, Dictionary<int, string> aliases)
        {
            foreach (var (nodeId, _) in aliases)
            {
                Source? camera = ResolveCamera(nodeId);
                if (camera == null) continue;

                bool driverMode = false;
                try { driverMode = ManagerWrapper.Instance.GetDriverMode(nodeId); } catch (Exception) { /* not a driver-mode capable node */ }
                int fpsLimit = camera.FpsLimit ?? -1;
                string status = $"{camera.ActiveProfileIndex}|{driverMode}|{fpsLimit}";

                foreach (int ntSinkId in ntSinkIds)
                {
                    var key = (ntSinkId, nodeId);
                    if (PublishedStatus.TryGetValue(key, out var last) && last.status == status && DateTime.UtcNow - last.at < StatusRefresh)
                        continue;
                    ManagerWrapper.Instance.SetNetworkTablesSinkNodeStatus(ntSinkId, nodeId.ToString(), camera.ActiveProfileIndex, driverMode, fpsLimit);
                    PublishedStatus[key] = (status, DateTime.UtcNow);
                }
            }
        }

        // the camera a detector node runs on: the source whose active profile owns it, the source itself, or the camera a standalone detector is bound to
        private static Source? ResolveCamera(int nodeId)
        {
            Source? owner = SourceManager.Instance.GetAllSourceIds()
                .Select(id => SourceManager.Instance.GetSourceById(id))
                .FirstOrDefault(s => s != null && (s.ActiveDetectionSinkId == nodeId || s.Id == nodeId));
            if (owner != null) return owner;

            Source? bound = SinkManager.Instance.GetSinkById(nodeId)?.Source;
            return bound == null ? null : SourceManager.Instance.GetSourceById(bound.Id);
        }

        private static void ApplyLedRequest(int value)
        {
            if (value < (int)LedMode.Default || value > (int)LedMode.Blink)
            {
                Console.WriteLine($"NT ledMode request {value} ignored: expected -1 (default), 0 (off), 1 (on) or 2 (blink)");
                return;
            }
            LedController.Instance.SetMode((LedMode)value);
        }

        // entries are {"sourceId": "<nodeId>", "pipelineIndex"?, "driverMode"?, "fpsLimit"?, "inputSnapshots"?, "outputSnapshots"?}
        // (see NetworkTablesSink::PollConfigRequests); driverMode applies to that detector node, everything else to the camera it runs on.
        private static void ApplyConfigRequests(string json)
        {
            if (string.IsNullOrEmpty(json) || json == "[]") return;
            using JsonDocument doc = JsonDocument.Parse(json);
            foreach (JsonElement entry in doc.RootElement.EnumerateArray())
            {
                if (!int.TryParse(entry.GetProperty("sourceId").GetString(), out int nodeId)) continue;

                if (entry.TryGetProperty("driverMode", out JsonElement driverMode))
                {
                    try
                    {
                        ManagerWrapper.Instance.SetDriverMode(nodeId, driverMode.GetBoolean());
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"NT driverMode request for {nodeId} ignored: {ex.Message}");
                    }
                }

                bool needsCamera = entry.TryGetProperty("pipelineIndex", out JsonElement pipelineIndex)
                    | entry.TryGetProperty("fpsLimit", out JsonElement fpsLimit)
                    | entry.TryGetProperty("inputSnapshots", out JsonElement inputSnapshots)
                    | entry.TryGetProperty("outputSnapshots", out JsonElement outputSnapshots);
                if (!needsCamera) continue;

                Source? camera = ResolveCamera(nodeId);
                if (camera == null)
                {
                    Console.WriteLine($"NT request for {nodeId} ignored: no camera runs that node");
                    continue;
                }

                if (entry.TryGetProperty("pipelineIndex", out pipelineIndex))
                {
                    try
                    {
                        int index = pipelineIndex.GetInt32();
                        if (camera.ActiveProfileIndex != index)
                            SourceManager.Instance.ActivateProfile(camera.Id, index);
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"NT pipelineIndex request for source {camera.Id} ignored: {ex.Message}");
                    }
                }

                if (entry.TryGetProperty("fpsLimit", out fpsLimit))
                {
                    try
                    {
                        SourceManager.Instance.SetFpsLimit(camera.Id, fpsLimit.GetInt32());
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"NT fpsLimit request for source {camera.Id} ignored: {ex.Message}");
                    }
                }

                if (entry.TryGetProperty("inputSnapshots", out inputSnapshots)) TakeSnapshot(camera.Id, SnapshotKind.Input);
                if (entry.TryGetProperty("outputSnapshots", out outputSnapshots)) TakeSnapshot(camera.Id, SnapshotKind.Output);
            }
        }

        // a snapshot waits for a fresh frame, so it runs off the tick
        private static void TakeSnapshot(int sourceId, SnapshotKind kind)
        {
            _ = Task.Run(() =>
            {
                try
                {
                    string? saved = SnapshotService.Instance.Save(sourceId, kind);
                    Console.WriteLine(saved == null
                        ? $"NT {kind} snapshot request for source {sourceId}: no frame available"
                        : $"NT {kind} snapshot saved: {saved}");
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"NT {kind} snapshot request for source {sourceId} failed: {ex.Message}");
                }
            });
        }
    }
}
