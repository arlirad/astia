using System.Net;
using Microsoft.AspNetCore.Hosting;
using Pkgsrv.Infrastructure.Postgres;
using Pkgsrv.Presentation.Api;
using Pkgsrv.Presentation.Composition;
using Pkgsrv.Presentation.Configuration;

namespace Pkgsrv.Presentation;

public static class PkgsrvHost
{
    public static WebApplication Create(
        string[] args,
        Action<IServiceCollection>? configureServices = null)
    {
        var builder = WebApplication.CreateBuilder(args);
        var listenOptions = PkgsrvListenOptions.FromConfiguration(builder.Configuration);
        var storageOptions = PkgsrvStorageOptions.FromConfiguration(builder.Configuration);
        var allowedSourceHosts = builder.Configuration
            .GetSection("Pkgsrv:Sources:AllowedHosts")
            .Get<string[]>() ?? Array.Empty<string>();

        builder.WebHost.UseUrls();
        builder.WebHost.ConfigureKestrel(options =>
        {
            if (listenOptions.TcpEnabled)
            {
                var endpoint = listenOptions.TcpEndpoint!;
                var host = endpoint.DnsSafeHost;

                if (host.Equals("localhost", StringComparison.OrdinalIgnoreCase))
                {
                    options.ListenLocalhost(endpoint.Port);
                }
                else
                {
                    options.Listen(IPAddress.Parse(host), endpoint.Port);
                }
            }
            else
            {
                options.ListenUnixSocket(listenOptions.UnixSocketPath);
            }
        });

        builder.Services.AddSingleton(listenOptions);
        builder.Services.AddPkgsrv(allowedSourceHosts, storageOptions);
        configureServices?.Invoke(builder.Services);

        var app = builder.Build();

        if (storageOptions.Provider == PkgsrvStorageProvider.Postgres)
        {
            app.Services
                .GetRequiredService<PostgresPackageSchemaValidator>()
                .ValidateAsync(CancellationToken.None)
                .GetAwaiter()
                .GetResult();
        }

        app.MapGet("/healthz", () => Results.Ok(new { status = "ok" }));
        app.MapPackageEndpoints();

        return app;
    }
}
