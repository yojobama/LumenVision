using System;
using System.Collections.Concurrent;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Hosting;

namespace Server
{
    // The web UI makes an MJPEG sink ("preview-mjpeg-<node>") when a WebRTC preview cannot connect and deletes it when the view closes. A browser that
    // is closed, crashes or loses its connection never gets to do that, so this deletes those sinks once nobody has read from them for a while.
    public sealed class MjpegPreviewJanitor : BackgroundService
    {
        public const string NamePrefix = "preview-mjpeg-";
        public static readonly TimeSpan IdleLimit = TimeSpan.FromSeconds(30);

        public static MjpegPreviewJanitor Instance { get; } = new MjpegPreviewJanitor();

        private readonly ConcurrentDictionary<int, DateTime> lastSeen = new ConcurrentDictionary<int, DateTime>();

        // a viewer read from this sink just now (the stream endpoint calls it while a stream is open, the frame endpoint on every poll)
        public static void Touch(int sinkId, DateTime? at = null) => Instance.lastSeen[sinkId] = at ?? DateTime.UtcNow;

        // Deletes the idle fallback sinks; a sink nobody has read from yet gets IdleLimit from the first time it is seen. Returns how many it deleted.
        public int Sweep(DateTime now)
        {
            var previews = SinkManager.Instance.GetAllSinks()
                .Where(s => s.Type == SinkType.MjpegSink && s.Name != null && s.Name.StartsWith(NamePrefix, StringComparison.Ordinal))
                .Select(s => s.Id).ToList();

            int deleted = 0;
            foreach (int id in previews)
            {
                DateTime seen = lastSeen.GetOrAdd(id, now);
                if (now - seen <= IdleLimit) continue;
                try
                {
                    SinkManager.Instance.DeleteSink(id);
                    deleted++;
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Deleting the idle preview sink {id} failed: {ex.Message}");
                    continue;
                }
                lastSeen.TryRemove(id, out _);
            }

            foreach (int id in lastSeen.Keys.Except(previews).ToList()) lastSeen.TryRemove(id, out _);
            return deleted;
        }

        public void Forget() => lastSeen.Clear();

        protected override async Task ExecuteAsync(CancellationToken stoppingToken)
        {
            while (!stoppingToken.IsCancellationRequested)
            {
                try
                {
                    await Task.Delay(TimeSpan.FromSeconds(10), stoppingToken);
                    Sweep(DateTime.UtcNow);
                }
                catch (OperationCanceledException)
                {
                    return;
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Sweeping the idle preview sinks failed: {ex.Message}");
                }
            }
        }
    }
}
