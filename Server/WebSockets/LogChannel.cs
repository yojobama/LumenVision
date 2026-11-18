using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Hosting;
using System;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;

namespace Server.WebSockets
{
    // /ws/logs: every new log entry as one JSON text message, from the connection onwards (read the history with GET /api/log/entries first).
    // ?level=warning limits it to that severity and above. A client that cannot keep up loses its oldest messages instead of slowing the server.
    public static class LogChannel
    {
        public static async Task HandleAsync(HttpContext context)
        {
            if (!context.WebSockets.IsWebSocketRequest)
            {
                context.Response.StatusCode = StatusCodes.Status400BadRequest;
                return;
            }
            LogSeverity minimum = LogSeverity.Debug;
            if (context.Request.Query.TryGetValue("level", out var level) && !Enum.TryParse(level.ToString(), ignoreCase: true, out minimum))
            {
                context.Response.StatusCode = StatusCodes.Status400BadRequest;
                return;
            }

            using WebSocket socket = await context.WebSockets.AcceptWebSocketAsync();
            var queue = Channel.CreateBounded<LogEntry>(new BoundedChannelOptions(2000) { FullMode = BoundedChannelFullMode.DropOldest, SingleReader = true });
            Action<LogEntry> onEntry = entry => { if (entry.Level >= minimum) queue.Writer.TryWrite(entry); };
            LogHub.Instance.Added += onEntry;
            using var finished = CancellationTokenSource.CreateLinkedTokenSource(context.RequestAborted);
            // the server stopping ends the connection too (see StateChannel.CloseWhenStopping): a socket left parked in ReceiveAsync keeps Kestrel's graceful
            // shutdown waiting for the host's 30 s timeout
            using CancellationTokenRegistration stopping = StateChannel.CloseWhenStopping(context, socket, finished);
            try
            {
                // the receive loop only notices the client closing
                Task receiving = Task.Run(async () =>
                {
                    var buffer = new byte[256];
                    try
                    {
                        while (socket.State == WebSocketState.Open)
                        {
                            var result = await socket.ReceiveAsync(buffer, finished.Token);
                            if (result.MessageType == WebSocketMessageType.Close)
                            {
                                // answers the client's close; when the server sent the first one (it is stopping) the handshake is already complete
                                if (socket.State == WebSocketState.CloseReceived)
                                    await socket.CloseOutputAsync(WebSocketCloseStatus.NormalClosure, null, CancellationToken.None);
                                break;
                            }
                        }
                    }
                    catch (Exception ex) when (ex is WebSocketException or OperationCanceledException) { /* dropped */ }
                    finally { finished.Cancel(); }
                });

                await foreach (LogEntry entry in queue.Reader.ReadAllAsync(finished.Token))
                {
                    if (socket.State != WebSocketState.Open) break;
                    byte[] payload = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(entry));
                    await socket.SendAsync(payload, WebSocketMessageType.Text, true, finished.Token);
                }
                await receiving;
            }
            catch (Exception ex) when (ex is WebSocketException or OperationCanceledException or ObjectDisposedException or InvalidOperationException)
            {
                // client went away
            }
            finally
            {
                LogHub.Instance.Added -= onEntry;
            }
        }
    }
}
