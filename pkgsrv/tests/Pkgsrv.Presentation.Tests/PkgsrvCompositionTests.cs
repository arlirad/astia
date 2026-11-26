using Arlirad.Ervi.Mediator.Interfaces;
using Microsoft.AspNetCore.Routing;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Pkgsrv.Application.Interfaces.Builds;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Features.Packages.List;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Packages;
using Pkgsrv.Infrastructure.Persistence.Postgres;
using Pkgsrv.Presentation.Caching;
using Pkgsrv.Presentation.Composition;
using Pkgsrv.Presentation.Configuration;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Presentation.Tests;

public sealed class PkgsrvCompositionTests
{
    [Fact]
    public async Task PkgsrvHost_BuildsWithPackageRoutesAndErviMediator()
    {
        await using var app = PkgsrvHost.Create([]);

        Assert.NotNull(app.Services.GetRequiredService<IMediator>());
        Assert.IsType<PackageJsonCache>(
            app.Services.GetRequiredService<IPackageJsonCache>());
    }

    [Fact]
    public async Task PkgsrvHost_MapsGetPackageRouteForCreatedLocation()
    {
        await using var app = PkgsrvHost.Create([]);

        var endpoint = ((IEndpointRouteBuilder)app).DataSources
            .SelectMany(source => source.Endpoints)
            .OfType<RouteEndpoint>()
            .Single(candidate => candidate.RoutePattern.RawText == "/packages/{name}/{version}");
        var methods = endpoint.Metadata.GetMetadata<HttpMethodMetadata>()?.HttpMethods;

        Assert.NotNull(methods);
        Assert.Contains("GET", methods);
    }

    [Fact]
    public async Task PkgsrvHost_MapsPublishAndDownloadRoutes()
    {
        await using var app = PkgsrvHost.Create([]);

        var routes = ((IEndpointRouteBuilder)app).DataSources
            .SelectMany(source => source.Endpoints)
            .OfType<RouteEndpoint>()
            .Select(endpoint => endpoint.RoutePattern.RawText)
            .ToArray();

        Assert.Contains("/packages/{name}/{version}/publish", routes);
        Assert.Contains("/packages/{name}/{version}/download", routes);
    }

    [Fact]
    public async Task AddPkgsrv_RegistersErviMediatorAndDefaultDenyAuthorization()
    {
        var services = new ServiceCollection();
        services.AddPkgsrv(["github.com"]);

        await using var provider = services.BuildServiceProvider();
        await using var scope = provider.CreateAsyncScope();

        var authorization = scope.ServiceProvider.GetRequiredService<IIrvaAuthorization>();
        var decision = await authorization.AuthorizeAsync(
            TestPackages.Actor,
            PackageAction.Read,
            null,
            CancellationToken.None);

        Assert.False(decision.IsAllowed);

        var sandbox = scope.ServiceProvider.GetRequiredService<IBuildSandbox>();
        var package = TestPackages.CreatePackage();
        var build = await sandbox.ExecuteAsync(
            new BuildSandboxRequest(package.Key, package.Manifest.Source, package.BuildPolicy),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, build.Status);
        Assert.NotNull(scope.ServiceProvider.GetRequiredService<IMediator>());
    }

    [Fact]
    public void AddPkgsrv_DefaultsToInMemoryRepository()
    {
        var services = new ServiceCollection();
        services.AddPkgsrv([]);

        using var provider = services.BuildServiceProvider();

        Assert.IsType<InMemoryPackageRepository>(
            provider.GetRequiredService<IPackageRepository>());
    }

    [Fact]
    public void AddPkgsrv_SelectsPostgresRepositoryWhenConfigured()
    {
        var configuration = new ConfigurationBuilder()
            .AddInMemoryCollection(new Dictionary<string, string?>
            {
                ["Pkgsrv:Database:ConnectionString"] =
                    "Host=localhost;Database=pkgsrv;Username=pkgsrv;Password=placeholder",
                ["Pkgsrv:Database:InitializeSchema"] = "true"
            })
            .Build();
        var databaseOptions = PkgsrvDatabaseOptions.FromConfiguration(configuration);
        var services = new ServiceCollection();
        services.AddPkgsrv([], databaseOptions);

        using var provider = services.BuildServiceProvider();

        Assert.IsType<PostgresPackageRepository>(
            provider.GetRequiredService<IPackageRepository>());
        Assert.IsType<PostgresPackageRepository>(
            provider.GetRequiredService<IPackageIndex>());
        Assert.NotNull(provider.GetRequiredService<IPackageSchemaMigrator>());
    }

    [Fact]
    public async Task AddPkgsrv_DispatchesThroughErviAndSurfacesAuthorizedReads()
    {
        var services = new ServiceCollection();
        services.AddPkgsrv([]);
        services.AddScoped<IIrvaAuthorization, AlwaysAllowAuthorization>();

        await using var provider = services.BuildServiceProvider();
        await using var scope = provider.CreateAsyncScope();
        var mediator = scope.ServiceProvider.GetRequiredService<IMediator>();

        var packages = await mediator.Send(
            new ListPackagesQuery(null, TestPackages.Actor),
            CancellationToken.None);

        Assert.Empty(packages);
    }

    private sealed class AlwaysAllowAuthorization : IIrvaAuthorization
    {
        public Task<AuthorizationResult> AuthorizeAsync(
            AuthorizationContext context,
            PackageAction action,
            PackageKey? package,
            CancellationToken cancellationToken) =>
            Task.FromResult(AuthorizationResult.Allow());
    }
}
