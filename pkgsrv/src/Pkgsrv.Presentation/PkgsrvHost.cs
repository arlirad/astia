using System.Net;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.DependencyInjection;
using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Application.Interfaces.Builds;
using Pkgsrv.Application.Interfaces.Caching;
using Pkgsrv.Application.Interfaces.Sources;
using Pkgsrv.Infrastructure.Artifacts;
using Pkgsrv.Infrastructure.Builds;
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
        Action<IServiceCollection>? configureServices = null,
        Func<string, long, long, long, bool>? artifactRootValidationOverride = null)
    {
        var builder = WebApplication.CreateBuilder(args);
        var listenOptions = PkgsrvListenOptions.FromConfiguration(builder.Configuration);
        var databaseOptions = PkgsrvDatabaseOptions.FromConfiguration(builder.Configuration);
        var cacheOptions = PackageJsonCacheOptions.FromConfiguration(builder.Configuration);
        var artifactOptions = PkgsrvArtifactOptions.FromConfiguration(builder.Configuration);
        var signingOptions = PkgsrvSigningOptions.FromConfiguration(builder.Configuration);
        var buildOptions = PkgsrvBuildOptions.FromConfiguration(builder.Configuration);
        var allowedSourceHosts = builder.Configuration
            .GetSection("Pkgsrv:Sources:AllowedHosts")
            .Get<string[]>() ?? Array.Empty<string>();
        var egressProxy = builder.Configuration["Pkgsrv:Sources:EgressProxy"]?.Trim();

        if (buildOptions.Enabled)
        {
            if (artifactOptions.Root is null)
            {
                throw new InvalidOperationException(
                    "Pkgsrv:Artifacts:Root must be configured to an existing private 0700 service-owned directory before builds can be enabled.");
            }

            if (artifactOptions.QuotaBytes <= 0 || artifactOptions.QuotaInodes <= 0)
            {
                throw new InvalidOperationException(
                    "Pkgsrv:Artifacts:QuotaBytes and Pkgsrv:Artifacts:QuotaInodes must be configured before builds can be enabled.");
            }

            var requiredArtifactBytes = checked(buildOptions.MaxArtifactBytes + (1024L * 1024));
            string? artifactRootDetail = null;
            var artifactRootValid = artifactRootValidationOverride is null
                ? ArtifactRootValidator.TryValidate(
                    artifactOptions.Root,
                    artifactOptions.QuotaBytes,
                    artifactOptions.QuotaInodes,
                    requiredArtifactBytes,
                    out artifactRootDetail)
                : artifactRootValidationOverride(
                    artifactOptions.Root,
                    artifactOptions.QuotaBytes,
                    artifactOptions.QuotaInodes,
                    requiredArtifactBytes);

            if (!artifactRootValid)
            {
                throw new InvalidOperationException(
                    artifactRootDetail
                    ?? "Pkgsrv:Artifacts:Root is not a safe, durable, quota-bounded artifact filesystem.");
            }

            if (!GitSourceCheckout.IsValidEgressProxy(egressProxy))
            {
                throw new InvalidOperationException(
                    "Pkgsrv:Sources:EgressProxy must be a valid operator-configured HTTP(S) proxy URL without credentials before builds can be enabled.");
            }
        }

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

        builder.Services.AddSingleton(buildOptions);

        if (artifactOptions.Root is not null)
        {
            builder.Services.AddSingleton<IArtifactStore>(
                new FileSystemArtifactStore(artifactOptions.Root));
        }

        if (signingOptions.CanSign)
        {
            var signer = new RsaPackageSigner(signingOptions.PrivateKeyPath!);
            var verifier = new RsaPackageSignatureVerifier(
                signingOptions.PublicKeyPath ?? string.Empty,
                signingOptions.PrivateKeyPath);

            builder.Services.AddSingleton<IPackageSigner>(signer);
            builder.Services.AddSingleton<IPackageSignatureVerifier>(verifier);
            builder.Services.AddSingleton<IPackagePublisher>(provider =>
                new VerifyingPackagePublisher(
                    verifier,
                    provider.GetRequiredService<IArtifactStore>()));
        }
        else if (signingOptions.PublicKeyPath is not null)
        {
            builder.Services.AddSingleton<IPackageSignatureVerifier>(
                new RsaPackageSignatureVerifier(signingOptions.PublicKeyPath));
        }

        if (buildOptions.Enabled)
        {
            builder.Services.AddSingleton<IBuildProcessRunner, SystemBuildProcessRunner>();
            builder.Services.AddSingleton<ISourceCheckout>(provider =>
                new GitSourceCheckout(
                    provider.GetRequiredService<IBuildProcessRunner>(),
                    egressProxy));
            builder.Services.AddSingleton<IBuildSandbox>(provider =>
                new BubblewrapBuildSandbox(
                    provider.GetRequiredService<IBuildProcessRunner>(),
                    provider.GetRequiredService<ISourceCheckout>(),
                    provider.GetRequiredService<IArtifactStore>(),
                    provider.GetRequiredService<ISourceAllowlist>(),
                    buildOptions.ToSandboxOptions(
                        artifactOptions.Root,
                        artifactOptions.QuotaBytes,
                        artifactOptions.QuotaInodes,
                        checked(buildOptions.MaxArtifactBytes + (1024L * 1024)))));
        }

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
                .GetRequiredService<IPackageSchemaMigrator>()
                .EnsureUpToDateAsync(CancellationToken.None)
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

        var databaseOptions = app.Services.GetRequiredService<PkgsrvDatabaseOptions>();

        if (databaseOptions.Provider != PkgsrvDatabaseProvider.Postgres)
        {
            return;
        }

        var migrator = app.Services.GetRequiredService<IPackageSchemaMigrator>();

        if (databaseOptions.InitializeSchema)
        {
            await migrator.MigrateAsync(cancellationToken);
        }
        else
        {
            await migrator.EnsureUpToDateAsync(cancellationToken);
        }
    }
}
