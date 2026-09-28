using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Hosting;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace Server.WebSockets
{
    public record struct NodeStatsDto(double Fps, long LatencyUs);
    public record struct DeviceStatsDto(int CpuUsagePercent, int RamUsageMb, int DiskUsagePercent, int TemperatureC);
    public record struct SinkStateDto(Sink Sink, bool IsRunning);
    public record struct StateSnapshotDto(Source[] Sources, SinkStateDto[] Sinks, DeviceStatsDto Device, Dictionary<int, NodeStatsDto> NodeStats);

    // Pushes one consolidated state snapshot to every connected client on a fixed tick, computed once per tick regardless of client count.
    // HandleAsync (/ws/state) registers a socket and parks on its receive loop; StateChannelBroadcaster drives RunAsync as the single sender.
    public class StateChannel
    {
        public static StateChannel Instance { get; } = new();

        private static readonly TimeSpan TickInterval = TimeSpan.FromSeconds(1);
        // per-node id: frame count and sample time from the previous tick, used to derive FPS from the count delta
        private readonly Dictionary<int, (ulong count, DateTime at)> _lastSample = new();
        private readonly ConcurrentDictionary<Guid, WebSocket> _clients = new();

        public async Task HandleAsync(HttpContext context)
        {
            if (!context.WebSockets.IsWebSocketRequest)
            {
                context.Response.StatusCode = StatusCodes.Status400BadRequest;
                return;
            }

            using WebSocket socket = await context.WebSockets.AcceptWebSocketAsync();
            var id = Guid.NewGuid();
            _clients[id] = socket;
            try
            {
                // one-way channel; this loop only detects the close handshake or a dropped connection
                var buffer = new byte[256];
                while (socket.State == WebSocketState.Open)
                {
                    var result = await socket.ReceiveAsync(buffer, context.RequestAborted);
                    if (result.MessageType == WebSocketMessageType.Close)
                    {
                        await socket.CloseOutputAsync(WebSocketCloseStatus.NormalClosure, null, CancellationToken.None);
                        break;
                    }
                }
            }
            catch (Exception ex) when (ex is WebSocketException or OperationCanceledException)
            {
                // client vanished without a close handshake - just drop it
            }
            finally
            {
                _clients.TryRemove(id, out _);
            }
        }

        private async Task BroadcastAsync(string json)
        {
            var payload = new ArraySegment<byte>(Encoding.UTF8.GetBytes(json));
            foreach (var (id, socket) in _clients)
            {
                if (socket.State != WebSocketState.Open) continue;
                try
                {
                    await socket.SendAsync(payload, WebSocketMessageType.Text, true, CancellationToken.None);
                }
                catch (Exception ex) when (ex is WebSocketException or ObjectDisposedException)
                {
                    _clients.TryRemove(id, out _); // one bad client must not stop the others' tick
                }
            }
        }

        public async Task RunAsync(CancellationToken cancellationToken)
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                try
                {
                    // built every tick even with no clients: FPS is a tick-to-tick delta, so skipping ticks would skew the next value
                    string json = JsonSerializer.Serialize(BuildSnapshot());
                    await BroadcastAsync(json);
                }
                catch (Exception ex)
                {
                    // a failed broadcast tick (e.g. a client disconnecting mid-send) must not stop the loop
                    Console.WriteLine($"StateChannel broadcast tick failed: {ex.Message}");
                }

                try
                {
                    await Task.Delay(TickInterval, cancellationToken);
                }
                catch (TaskCanceledException)
                {
                    break;
                }
            }
        }

        private StateSnapshotDto BuildSnapshot()
        {
            var sourceIds = SourceManager.Instance.GetAllSourceIds();
            var sinkIds = SinkManager.Instance.getAllSinkIds();

            var sources = sourceIds.Select(id => SourceManager.Instance.GetSourceById(id)).ToArray();
            var sinks = sinkIds.Select(id => new SinkStateDto(
                SinkManager.Instance.GetSinkById(id),
                SinkManager.Instance.IsSinkRunning(id))).ToArray();

            // dual-role sinks share the source id space, so tracking every source and sink id covers cameras and detectors;
            // GetFrameCount/GetLatencyUs return 0 for the half that doesn't apply.
            var trackedIds = sourceIds.Concat(sinkIds).Distinct();
            var nodeStats = new Dictionary<int, NodeStatsDto>();
            DateTime now = DateTime.UtcNow;

            foreach (int id in trackedIds)
            {
                ulong count = ManagerWrapper.Instance.GetFrameCount(id);
                long latencyUs = ManagerWrapper.Instance.GetLatencyUs(id);

                double fps = 0;
                if (_lastSample.TryGetValue(id, out var last) && count >= last.count)
                {
                    double elapsedSeconds = (now - last.at).TotalSeconds;
                    if (elapsedSeconds > 0) fps = (count - last.count) / elapsedSeconds;
                }
                _lastSample[id] = (count, now);

                nodeStats[id] = new NodeStatsDto(fps, latencyUs);
            }

            var resourceInfo = LinuxResourceMonitor.Instance.GetLatestResourceInfo();
            var device = new DeviceStatsDto(
                (int)resourceInfo.CpuUsagePercent,
                (int)resourceInfo.UsedMemoryMB,
                (int)resourceInfo.RootDiskUsage.UsedPercent,
                ManagerWrapper.Instance.GetCpuTemperature());

            return new StateSnapshotDto(sources, sinks, device, nodeStats);
        }
    }

    // Runs StateChannel's broadcast tick for the lifetime of the host.
    public sealed class StateChannelBroadcaster : BackgroundService
    {
        protected override Task ExecuteAsync(CancellationToken stoppingToken) => StateChannel.Instance.RunAsync(stoppingToken);
    }
}
