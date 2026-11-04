using Microsoft.AspNetCore.Http;
using System.IO;
using System.Text;
using System.Threading.Tasks;

namespace Server.Web
{
    // EmbedIO-style IHttpContext helpers on ASP.NET Core's HttpContext. SendStringAsync writes the encoding's
    // preamble (Encoding.UTF8 = BOM, UTF8Encoding(false) = none).
    public static class HttpContextCompat
    {
        public static async Task SendStringAsync(this HttpContext context, string content, string contentType, Encoding encoding)
        {
            context.Response.ContentType = $"{contentType}; charset={encoding.WebName}";
            byte[] preamble = encoding.GetPreamble();
            byte[] body = encoding.GetBytes(content);
            context.Response.ContentLength = preamble.Length + body.Length;
            if (preamble.Length > 0) await context.Response.Body.WriteAsync(preamble, context.RequestAborted);
            await context.Response.Body.WriteAsync(body, context.RequestAborted);
        }

        public static async Task<string> GetRequestBodyAsStringAsync(this HttpContext context)
        {
            using var reader = new StreamReader(context.Request.Body, Encoding.UTF8);
            return await reader.ReadToEndAsync();
        }

        // The request body as a T, or a 400 saying what is wrong with it: an empty body, one that is not JSON, or the JSON literal null. Controllers that
        // call JsonSerializer.Deserialize on the raw body themselves answer an empty body with a 500.
        public static async Task<T> ReadJsonBodyAsync<T>(this HttpContext context)
        {
            string body = await context.GetRequestBodyAsStringAsync();
            if (string.IsNullOrWhiteSpace(body)) throw ApiException.BadRequest("the request needs a JSON body");
            try
            {
                T? value = System.Text.Json.JsonSerializer.Deserialize<T>(body);
                if (value is null) throw ApiException.BadRequest("the request body must not be null");
                return value;
            }
            catch (System.Text.Json.JsonException ex)
            {
                throw ApiException.BadRequest("the request body is not valid JSON for this call: " + ex.Message);
            }
        }

        public static Stream OpenRequestStream(this HttpContext context) => context.Request.Body;
    }
}
