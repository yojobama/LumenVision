using Microsoft.AspNetCore.Http;
using System;
using System.Text;
using System.Threading.Tasks;

namespace Server.Web
{
    // Throwable HTTP status; ApiExceptionMiddleware turns it into the matching status code.
    public sealed class ApiException : Exception
    {
        public int StatusCode { get; }

        public ApiException(int statusCode, string? message = null) : base(message ?? "")
        {
            StatusCode = statusCode;
        }

        public static ApiException NotFound(string? message = null) => new(StatusCodes.Status404NotFound, message);
        public static ApiException BadRequest(string? message = null) => new(StatusCodes.Status400BadRequest, message);
    }

    // Maps ApiException to its status code and any other unhandled exception to 500 (logged),
    // with short plain-text bodies.
    public sealed class ApiExceptionMiddleware
    {
        private readonly RequestDelegate _next;

        public ApiExceptionMiddleware(RequestDelegate next) => _next = next;

        public async Task InvokeAsync(HttpContext context)
        {
            try
            {
                await _next(context);
            }
            catch (ApiException ex)
            {
                await WriteError(context, ex.StatusCode, ex.Message);
            }
            catch (OperationCanceledException) when (context.RequestAborted.IsCancellationRequested)
            {
                // client went away mid-request (e.g. an MJPEG viewer closing its tab) - not an error
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Unhandled exception serving {context.Request.Method} {context.Request.Path}: {ex}");
                await WriteError(context, StatusCodes.Status500InternalServerError, ex.GetType().Name + ": " + ex.Message);
            }
        }

        private static async Task WriteError(HttpContext context, int status, string message)
        {
            if (context.Response.HasStarted) return; // too late to change the status line
            context.Response.Clear();
            context.Response.StatusCode = status;
            context.Response.ContentType = "text/plain; charset=utf-8";
            await context.Response.WriteAsync(message, Encoding.UTF8);
        }
    }
}
