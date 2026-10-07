using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http.Features;
using Microsoft.AspNetCore.Mvc.ApplicationParts;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.FileProviders;
using Microsoft.Extensions.Hosting;
using Server.HttpModules;
using Server.Web;
using Server.WebSockets;
using System.Text.Json.Serialization;

namespace Server
{
    internal class Program
    {
        // "*" rather than "localhost" so the WebUI and API are reachable from other machines; ASPNETCORE_URLS overrides it.
        //
        // 5800: FRC field networks pass ports 5800-5810 for team use. WebRTC media uses 5801-5809 (LumenCore/WebRTCSink.cpp);
        // 5810 is the roboRIO's NT4 server.
        private const string DefaultUrl = "http://*:5800";

        private static WebApplication CreateWebServer(string[] args)
        {
            string wwwrootPath = Path.Combine(AppContext.BaseDirectory, "wwwroot");
            bool hasWebUi = Directory.Exists(wwwrootPath);

            var builder = WebApplication.CreateBuilder(new WebApplicationOptions
            {
                Args = args,
                ContentRootPath = AppContext.BaseDirectory,
                WebRootPath = hasWebUi ? wwwrootPath : null,
            });

            // sd_notify/SIGTERM integration under lumenvision.service
            builder.Host.UseSystemd();

            if (Environment.GetEnvironmentVariable("ASPNETCORE_URLS") == null)
                builder.WebHost.UseUrls(DefaultUrl);

            builder.WebHost.ConfigureKestrel(o =>
            {
                // model (.onnx/.rknn) and video uploads routinely exceed Kestrel's 30 MB default limit
                o.Limits.MaxRequestBodySize = null;
                // a few controllers read request bodies through a synchronous StreamReader
                o.AllowSynchronousIO = true;
            });
            builder.Services.Configure<FormOptions>(o =>
            {
                o.MultipartBodyLengthLimit = long.MaxValue;
                o.ValueLengthLimit = int.MaxValue;
            });

            builder.Services.AddCors(o => o.AddDefaultPolicy(p => p.AllowAnyOrigin().AllowAnyHeader().AllowAnyMethod()));

            builder.Services
                .AddControllers(o =>
                {
                    o.Conventions.Add(new ApiPrefixConvention());
                    o.Filters.Add(new RejectUnparseableParametersFilter());
                    // stops MVC treating non-nullable string parameters as implicitly [Required]; omitted optional names bind null
                    o.SuppressImplicitRequiredAttributeForNonNullableReferenceTypes = true;
                    // keeps a Task<string> action a JSON string on the wire (quoted, application/json) rather than bare text/plain;
                    // endpoints that send raw text (SDP, pre-rendered JSON) write their bodies directly via SendStringAsync.
                    o.OutputFormatters.RemoveType<Microsoft.AspNetCore.Mvc.Formatters.StringOutputFormatter>();
                })
                .ConfigureApplicationPartManager(m =>
                {
                    // Controllers come from RegisteredControllers.All rather than an assembly scan, matching Server/OpenApi's generated document.
                    foreach (var provider in m.FeatureProviders.OfType<Microsoft.AspNetCore.Mvc.Controllers.ControllerFeatureProvider>().ToList())
                        m.FeatureProviders.Remove(provider);
                    m.FeatureProviders.Add(new RegisteredControllerFeatureProvider());
                })
                .AddJsonOptions(o =>
                {
                    // PascalCase on the wire (as /ws/state and data.json); enums stay numeric, matching OpenApiGenerator's schemas.
                    o.JsonSerializerOptions.PropertyNamingPolicy = null;
                    // NaN/Infinity (e.g. a mean over zero samples) must not fail the endpoint
                    o.JsonSerializerOptions.NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals;
                });

            builder.Services.AddHostedService<StateChannelBroadcaster>();
            builder.Services.AddHostedService<NetworkTablesControlService>();

            var app = builder.Build();

            app.UseMiddleware<ApiExceptionMiddleware>();

            if (hasWebUi)
            {
                // Serves the built React WebUI (npm run build in webui, copied into wwwroot by Server.csproj).
                app.UseDefaultFiles();
                app.UseStaticFiles();
            }
            else
            {
                Console.WriteLine($"WARNING: wwwroot not found at '{wwwrootPath}' - WebUI will not be served (API is still available under /api)");
            }

            // After static files: routing earlier would select the SPA fallback endpoint for /assets/*.js,
            // and the static file middleware skips requests that already have a selected endpoint.
            app.UseRouting();
            app.UseCors();
            app.UseWebSockets(new WebSocketOptions { KeepAliveInterval = TimeSpan.FromSeconds(15) });

            // push channel broadcasting state to the webui (see StateChannel)
            app.Map("/ws/state", StateChannel.Instance.HandleAsync);
            // Outside /api: a long-lived response, not a REST call.
            app.MapGet("/stream/mjpeg", MjpegStreamModule.HandleAsync);
            app.MapControllers();

            if (hasWebUi)
            {
                // SPA fallback: client-side routes (/graph, /sources, ...) get index.html; /api/* is excluded so unknown API routes return 404.
                app.MapFallbackToFile("{*path:regex(^(?!api(/|$)).*$)}", "index.html",
                    new StaticFileOptions { FileProvider = new PhysicalFileProvider(wwwrootPath) });
            }

            return app;
        }

        static void Main(string[] args)
        {
            LinuxResourceMonitor.Instance.StartMonitoring();
            Thread.Sleep(3000); 

            Console.WriteLine("Loading database...");
            DB.Instance.Load();
            // a network change nobody confirmed before the last shutdown is undone
            _ = NetworkService.Instance.RecoverAsync().ContinueWith(t => { if (t.IsFaulted) Console.WriteLine($"[network] recovery failed: {t.Exception?.GetBaseException().Message}"); });
            DB.Instance.Verify();

            Console.WriteLine("Starting HTTP server...");
            var app = CreateWebServer(args);
            // blocks until SIGTERM/SIGINT (systemd stop/restart, or Ctrl+C in a terminal)
            app.Run();
        }
    }
}
