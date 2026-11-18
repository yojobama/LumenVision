using System.Diagnostics;
using System.Net.WebSockets;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Server.WebSockets;
using Xunit;

namespace Server.Tests.Edge;

// Found on the board (imagetest44): `systemctl stop lumenvision` took exactly 30 s - the host's shutdown timeout - whenever the web UI was open, and 0.8 s
// without it. The /ws/state and /ws/logs handlers waited in ReceiveAsync on the request's own token only, so Kestrel's graceful shutdown could not finish
// while a browser tab was connected.
public class WebSocketShutdownTests
{
    // a real Kestrel host with the two endpoints, and a shutdown timeout long enough that waiting it out is unmistakable (the fix must not depend on it)
    private static async Task<(WebApplication App, Uri WsBase)> StartHostAsync()
    {
        var builder = WebApplication.CreateBuilder();
        builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.Configure<HostOptions>(o => o.ShutdownTimeout = TimeSpan.FromSeconds(25));
        WebApplication app = builder.Build();
        app.UseWebSockets();
        app.Map("/ws/state", StateChannel.Instance.HandleAsync);
        app.Map("/ws/logs", LogChannel.HandleAsync);
        await app.StartAsync();
        string address = app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.First();
        return (app, new Uri(address.Replace("http://", "ws://")));
    }

    // what a browser does: keeps reading, and answers a close frame from the server with its own
    private static async Task<WebSocketReceiveResult> ReadUntilCloseAsync(ClientWebSocket socket, bool answerTheClose)
    {
        var buffer = new byte[1024];
        while (true)
        {
            WebSocketReceiveResult result = await socket.ReceiveAsync(buffer, CancellationToken.None);
            if (result.MessageType != WebSocketMessageType.Close) continue;
            if (answerTheClose) await socket.CloseOutputAsync(WebSocketCloseStatus.NormalClosure, null, CancellationToken.None);
            return result;
        }
    }

    [Fact]
    [Trait("Category", "Edge")]
    public async Task StoppingTheHostEndsOpenStateAndLogConnectionsInsteadOfWaitingOutTheShutdownTimeout()
    {
        (WebApplication app, Uri wsBase) = await StartHostAsync();
        await using var _ = app;
        using var state = new ClientWebSocket();
        using var logs = new ClientWebSocket();
        await state.ConnectAsync(new Uri(wsBase, "/ws/state"), CancellationToken.None);
        await logs.ConnectAsync(new Uri(wsBase, "/ws/logs"), CancellationToken.None);
        Task<WebSocketReceiveResult> stateRead = ReadUntilCloseAsync(state, answerTheClose: true);
        Task<WebSocketReceiveResult> logsRead = ReadUntilCloseAsync(logs, answerTheClose: true);

        var clock = Stopwatch.StartNew();
        await app.StopAsync();
        clock.Stop();

        Assert.True(clock.Elapsed < TimeSpan.FromSeconds(1.5), $"stopping the host took {clock.Elapsed.TotalSeconds:F1} s with two answering WebSocket clients connected");
        // both clients were told the server is going away, not just dropped
        WebSocketReceiveResult stateClose = await stateRead.WaitAsync(TimeSpan.FromSeconds(5));
        WebSocketReceiveResult logsClose = await logsRead.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(WebSocketCloseStatus.EndpointUnavailable, stateClose.CloseStatus);
        Assert.Equal(WebSocketCloseStatus.EndpointUnavailable, logsClose.CloseStatus);
    }

    [Fact]
    [Trait("Category", "Edge")]
    public async Task AClientThatNeverAnswersTheCloseIsCutOffAfterTheAnswerTimeout()
    {
        (WebApplication app, Uri wsBase) = await StartHostAsync();
        await using var _ = app;
        using var state = new ClientWebSocket();
        using var logs = new ClientWebSocket();
        await state.ConnectAsync(new Uri(wsBase, "/ws/state"), CancellationToken.None);
        await logs.ConnectAsync(new Uri(wsBase, "/ws/logs"), CancellationToken.None);
        Task<WebSocketReceiveResult> stateRead = ReadUntilCloseAsync(state, answerTheClose: false);
        Task<WebSocketReceiveResult> logsRead = ReadUntilCloseAsync(logs, answerTheClose: false);

        var clock = Stopwatch.StartNew();
        await app.StopAsync();
        clock.Stop();

        // the silent clients delay the stop by about the answer timeout, not by the host's 25 s
        Assert.True(clock.Elapsed < StateChannel.CloseAnswerTimeout + TimeSpan.FromSeconds(4), $"stopping the host took {clock.Elapsed.TotalSeconds:F1} s with two silent WebSocket clients connected");
        Assert.Equal(WebSocketCloseStatus.EndpointUnavailable, (await stateRead.WaitAsync(TimeSpan.FromSeconds(5))).CloseStatus);
        Assert.Equal(WebSocketCloseStatus.EndpointUnavailable, (await logsRead.WaitAsync(TimeSpan.FromSeconds(5))).CloseStatus);
    }
}
