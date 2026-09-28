using Server.OpenApi;
using System.Text;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers
{
    // Serves the reflection-generated OpenAPI document (used by openapi-typescript and API tooling).
    internal class OpenApiController : ControllerBase
    {
        // Written as a raw string body: EmbedIO's Swan serialiser cannot handle System.Text.Json's JsonObject.
        // UTF8Encoding(false) avoids the BOM preamble, which strict JSON parsers reject.
        private static readonly Encoding Utf8NoBom = new UTF8Encoding(false);

        [HttpGet("openapi.json")]
        public async Task GetOpenApiDocument()
        {
            string json = OpenApiGenerator.Generate(RegisteredControllers.All).ToJsonString();
            await HttpContext.SendStringAsync(json, "application/json", Utf8NoBom);
        }
    }
}
