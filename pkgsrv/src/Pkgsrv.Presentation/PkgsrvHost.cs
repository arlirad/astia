using System.Net;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.DependencyInjection;
using Pkgsrv.Application.Interfaces.Caching;
using Pkgsrv.Infrastructure.Persistence.Postgres;
using Pkgsrv.Presentation.Api;
using Pkgsrv.Presentation.Caching;
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
        var databaseOptions = PkgsrvDatabaseOptions.FromConfiguration(builder.Configuration);
        var cacheOptions = PackageJsonCacheOptions.FromConfiguration(builder.Configuration);
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
        builder.Services.AddSingleton(cacheOptions);
        builder.Services.AddMemoryCache(options => options.SizeLimit = cacheOptions.SizeLimit);
        builder.Services.AddSingleton<PackageJsonCache>();
        builder.Services.AddSingleton<IPackageJsonCache>(provider =>
            provider.GetRequiredService<PackageJsonCache>());
        builder.Services.AddSingleton<IPackageCacheInvalidator>(provider =>
            provider.GetRequiredService<PackageJsonCache>());
        builder.Services.AddPkgsrv(allowedSourceHosts, databaseOptions);
        configureServices?.Invoke(builder.Services);

        var app = builder.Build();

        if (databaseOptions.Provider == PkgsrvDatabaseProvider.InMemory)
        {
            app.Logger.LogWarning(
                "Pkgsrv is using the in-memory package store. Package metadata will not survive restart.");
        }
        else if (!databaseOptions.InitializeSchema)
        {
            app.Services
                .GetRequiredService<IPackageSchemaValidator>()
                .ValidateAsync(CancellationToken.None)
                .GetAwaiter()
                .GetResult();
        }

        app.MapGet("/healthz", () => Results.Ok(new { status = "ok" }));
        app.MapPackageEndpoints();

        return app;
    }

    public static async Task InitializeAsync(
        WebApplication app,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(app);

        var initializer = app.Services.GetService<IPackageSchemaInitializer>();

        if (initializer is not null)
        {
            await initializer.InitializeAsync(cancellationToken);
        }

        var validator = app.Services.GetService<IPackageSchemaValidator>();

        if (validator is not null)
        {
            await validator.ValidateAsync(cancellationToken);
        }
    }
}
