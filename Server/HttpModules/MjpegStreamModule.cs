using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Http.Features;
using System;
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
            string frameBase64 = SinkManager.Instance.GetMjpegFrameBase64(sinkId);
            if (frameBase64.Length == 0)
            {
                context.Response.StatusCode = StatusCodes.Status204NoContent;
                return;
            }

            context.Response.ContentType = "image/jpeg";
            context.Response.Headers.CacheControl = "no-store";
            await context.Response.Body.WriteAsync(Convert.FromBase64String(frameBase64));
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

            string lastFrameBase64 = "";
            while (!abort.IsCancellationRequested)
            {
                MjpegPreviewJanitor.Touch(sinkId);
                string frameBase64 = SinkManager.Instance.GetMjpegFrameBase64(sinkId);
                if (frameBase64.Length > 0 && frameBase64 != lastFrameBase64)
                {
                    byte[] jpegBytes = Convert.FromBase64String(frameBase64);
                    byte[] header = Encoding.ASCII.GetBytes(
                        $"--{Boundary}\r\nContent-Type: image/jpeg\r\nContent-Length: {jpegBytes.Length}\r\n\r\n");
                    byte[] footer = Encoding.ASCII.GetBytes("\r\n");

                    try
                    {
                        await context.Response.Body.WriteAsync(header, abort);
                        await context.Response.Body.WriteAsync(jpegBytes, abort);
                        await context.Response.Body.WriteAsync(footer, abort);
                        await context.Response.Body.FlushAsync(abort);
                    }
                    catch (OperationCanceledException)
                    {
                        break; // client disconnected mid-frame - not a real error
                    }

                    lastFrameBase64 = frameBase64;
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
    }
}
