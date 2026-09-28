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
    //  <root>/config/recording          (bool)  -> SinkManager.SetAllRecording
    //  <root>/<nodeId>/config/driverMode (bool) -> SetDriverMode on that detector
    //  <root>/<nodeId>/config/pipelineIndex     -> activate that source's pipeline profile
    // and publishes <root>/status/recording back as confirmation. Requests are idempotent desired state, so several
    // NT sinks seeing the same write is harmless.
    public sealed class NetworkTablesControlService : BackgroundService
    {
        // fast enough for recording changes at the auto/teleop boundary to land within a frame or two; in-process calls only
        private static readonly TimeSpan TickInterval = TimeSpan.FromMilliseconds(100);

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

            // the latest recording request across all NT sinks wins (they all see the same topic)
            int recordingRequest = -1;
            foreach (int ntSinkId in ntSinkIds)
            {
                int request = ManagerWrapper.Instance.PollNetworkTablesSinkRecordingRequest(ntSinkId);
                if (request != -1) recordingRequest = request;
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

            bool recording = SinkManager.Instance.IsAnyRecording();
            foreach (int ntSinkId in ntSinkIds)
            {
                ManagerWrapper.Instance.SetNetworkTablesSinkRecordingStatus(ntSinkId, recording);
            }
        }

        // entries are {"sourceId": "<nodeId>", "pipelineIndex"?: int, "driverMode"?: bool} (see NetworkTablesSink::PollConfigRequests);
        // driverMode applies to that detector node, pipelineIndex to the camera source it runs on (mapped back from the node).
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

                if (entry.TryGetProperty("pipelineIndex", out JsonElement pipelineIndex))
                {
                    int? sourceId = SourceManager.Instance.GetAllSourceIds()
                        .Select(id => SourceManager.Instance.GetSourceById(id))
                        .Where(s => s != null && (s.ActiveDetectionSinkId == nodeId || s.Id == nodeId))
                        .Select(s => (int?)s!.Id)
                        .FirstOrDefault();
                    if (sourceId == null)
                    {
                        Console.WriteLine($"NT pipelineIndex request for {nodeId} ignored: no source runs that node");
                        continue;
                    }
                    try
                    {
                        Source source = SourceManager.Instance.GetSourceById(sourceId.Value);
                        int index = pipelineIndex.GetInt32();
                        if (source.ActiveProfileIndex != index)
                            SourceManager.Instance.ActivateProfile(sourceId.Value, index);
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"NT pipelineIndex request for source {sourceId} ignored: {ex.Message}");
                    }
                }
            }
        }
    }
}
