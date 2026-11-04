using System;
using System.Net.WebSockets;
using System.Threading;
using System.Threading.Tasks;
using Server.WebSockets;
using Xunit;

namespace Server.Tests.Edge;

// Found by the imagetest41 live test: browser connections that had stopped reading filled their send buffers, the broadcaster awaited them one after
// another with no limit, and every client of /ws/state - the web UI sits on its loading screen until the first snapshot - stopped getting updates.
public class StateChannelStalledClientTests
{
    // a client whose send never completes on its own: it ends only when the send is cancelled
    private sealed class StalledSocket : WebSocket
    {
        private WebSocketState _state = WebSocketState.Open;
        public override WebSocketCloseStatus? CloseStatus => null;
        public override string? CloseStatusDescription => null;
        public override WebSocketState State => _state;
        public override string? SubProtocol => null;
        public override void Abort() => _state = WebSocketState.Aborted;
        public override Task CloseAsync(WebSocketCloseStatus closeStatus, string? statusDescription, CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task CloseOutputAsync(WebSocketCloseStatus closeStatus, string? statusDescription, CancellationToken cancellationToken) => Task.CompletedTask;
        public override void Dispose() { }
        public override Task<WebSocketReceiveResult> ReceiveAsync(ArraySegment<byte> buffer, CancellationToken cancellationToken) => Task.Delay(Timeout.Infinite, cancellationToken).ContinueWith<WebSocketReceiveResult>(_ => throw new OperationCanceledException());
        public override Task SendAsync(ArraySegment<byte> buffer, WebSocketMessageType messageType, bool endOfMessage, CancellationToken cancellationToken) => Task.Delay(Timeout.Infinite, cancellationToken);
    }

    private sealed class CountingSocket : StalledSocketBase
    {
    }

    private abstract class StalledSocketBase : WebSocket
    {
        public int Sent;
        public override WebSocketCloseStatus? CloseStatus => null;
        public override string? CloseStatusDescription => null;
        public override WebSocketState State => WebSocketState.Open;
        public override string? SubProtocol => null;
        public override void Abort() { }
        public override Task CloseAsync(WebSocketCloseStatus closeStatus, string? statusDescription, CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task CloseOutputAsync(WebSocketCloseStatus closeStatus, string? statusDescription, CancellationToken cancellationToken) => Task.CompletedTask;
        public override void Dispose() { }
        public override Task<WebSocketReceiveResult> ReceiveAsync(ArraySegment<byte> buffer, CancellationToken cancellationToken) => throw new NotSupportedException();
        public override Task SendAsync(ArraySegment<byte> buffer, WebSocketMessageType messageType, bool endOfMessage, CancellationToken cancellationToken)
        {
            Interlocked.Increment(ref Sent);
            return Task.CompletedTask;
        }
    }

    [Fact]
    [Trait("Category", "Edge")]
    public async Task AClientThatStopsReadingIsDroppedAndTheOthersStillGetTheTick()
    {
        var channel = new StateChannel();
        var stalled = new StalledSocket();
        var healthy = new CountingSocket();
        channel.Track(stalled);
        channel.Track(healthy);

        Task broadcast = channel.BroadcastAsync("{}");
        Task finished = await Task.WhenAny(broadcast, Task.Delay(StateChannel.SendTimeout + TimeSpan.FromSeconds(5)));

        Assert.Same(broadcast, finished);
        await broadcast;
        Assert.Equal(1, healthy.Sent);
        Assert.Equal(1, channel.ClientCount);
        Assert.Equal(WebSocketState.Aborted, stalled.State);
    }
}
