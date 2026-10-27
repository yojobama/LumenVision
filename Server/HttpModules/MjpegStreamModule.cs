using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Http.Features;
using System;
using System.Buffers;
using System.Text;
using System.Threading.Tasks;

namespace Server.HttpModules
{
    // Serves MjpegSink's latest frame as a long-lived multipart/x-mixed-replace response (mapped at /stream/mjpeg).
    // A plain endpoint, not a controller action: it never returns until the client disconnects or the server shuts down.
    public static class MjpegStreamModule
    {
        // ~10fps poll cap: bounds how often the loop checks for a new frame, not the frame rate; unchanged frames are not resent.
        private static readonly TimeSpan PollInterval = TimeSpan.FromMilliseconds(100);
        private const string Boundary = "lumenvision-mjpeg-frame";
        private static readonly byte[] Footer = Encoding.ASCII.GetBytes("\r\n");
        // a 1280x720 preview JPEG is 100-300 KB; a larger one grows the buffer once (see SinkManager.CopyMjpegFrame)
        private const int InitialBufferBytes = 512 * 1024;

        // The latest frame as one JPEG (mapped at /stream/mjpeg/frame), for small views that poll instead of holding a stream open: a browser allows
        // only a handful of simultaneous connections to one host, and every open stream uses one. 204 until the sink has produced a frame.
        public static async Task HandleFrameAsync(HttpContext context)
        {
            if (!int.TryParse(context.Request.Query["SinkID"], out int sinkId))
            {
                context.Response.StatusCode = StatusCodes.Status400BadRequest;
                await context.Response.WriteAsync("SinkID query parameter is required");
                return;
            }

            MjpegPreviewJanitor.Touch(sinkId);
            byte[] buffer = ArrayPool<byte>.Shared.Rent(InitialBufferBytes);
            try
            {
                int length = SinkManager.Instance.CopyMjpegFrame(sinkId, ref buffer);
                if (length == 0)
                {
                    context.Response.StatusCode = StatusCodes.Status204NoContent;
                    return;
                }

                context.Response.ContentType = "image/jpeg";
                context.Response.Headers.CacheControl = "no-store";
                context.Response.ContentLength = length;
                await context.Response.Body.WriteAsync(buffer.AsMemory(0, length));
            }
            finally
            {
                ArrayPool<byte>.Shared.Return(buffer);
            }
        }

        public static async Task HandleAsync(HttpContext context)
        {
            if (!int.TryParse(context.Request.Query["SinkID"], out int sinkId))
            {
                context.Response.StatusCode = StatusCodes.Status400BadRequest;
                await context.Response.WriteAsync("SinkID query parameter is required");
                return;
            }

            context.Response.ContentType = $"multipart/x-mixed-replace; boundary={Boundary}";
            // no Content-Length -> Kestrel uses chunked transfer encoding; buffering off so every
            // frame reaches the <img> as soon as it's flushed rather than when a buffer fills
            context.Features.Get<IHttpResponseBodyFeature>()?.DisableBuffering();
            var abort = context.RequestAborted;

            byte[] buffer = ArrayPool<byte>.Shared.Rent(InitialBufferBytes);
            long lastSequence = 0;
            try
            {
                while (!abort.IsCancellationRequested)
                {
                    MjpegPreviewJanitor.Touch(sinkId);
                    long sequence = SinkManager.Instance.GetMjpegFrameSequence(sinkId);
                    if (sequence != 0 && sequence != lastSequence)
                    {
                        int length = SinkManager.Instance.CopyMjpegFrame(sinkId, ref buffer);
                        if (length > 0)
                        {
                            byte[] header = Encoding.ASCII.GetBytes(
                                $"--{Boundary}\r\nContent-Type: image/jpeg\r\nContent-Length: {length}\r\n\r\n");
                            try
                            {
                                await context.Response.Body.WriteAsync(header, abort);
                                await context.Response.Body.WriteAsync(buffer.AsMemory(0, length), abort);
                                await context.Response.Body.WriteAsync(Footer, abort);
                                await context.Response.Body.FlushAsync(abort);
                            }
                            catch (OperationCanceledException)
                            {
                                break; // client disconnected mid-frame - not a real error
                            }
                        }
                        lastSequence = sequence;
                    }

                    try
                    {
                        await Task.Delay(PollInterval, abort);
                    }
                    catch (TaskCanceledException)
                    {
                        break; // client disconnected (or server shutting down) - not a real error
                    }
                }
            }
            finally
            {
                ArrayPool<byte>.Shared.Return(buffer);
            }
        }
    }
}
