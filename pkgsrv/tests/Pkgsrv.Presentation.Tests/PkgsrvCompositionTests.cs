using Arlirad.Ervi.Mediator.Interfaces;
using Microsoft.AspNetCore.Routing;
using Microsoft.Extensions.DependencyInjection;
using Pkgsrv.Application.Abstractions.Builds;
using Pkgsrv.Application.Abstractions.Caching;
using Pkgsrv.Application.Abstractions.Persistence;
using Pkgsrv.Application.Abstractions.Security;
using Pkgsrv.Application.Caching;
using Pkgsrv.Application.Features.Packages.List;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Packages;
using Pkgsrv.Infrastructure.Postgres;
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

    [Fact]
    public void AddPkgsrv_SelectsInMemoryStoreAndCacheByDefault()
    {
        var services = new ServiceCollection();
        services.AddPkgsrv([]);

        using var provider = services.BuildServiceProvider();

        Assert.IsType<InMemoryPackageRepository>(
            provider.GetRequiredService<IPackageRepository>());
        Assert.IsType<JsonPackageReadModelCache>(
            provider.GetRequiredService<IPackageReadModelCache>());
    }

    [Fact]
    public void AddPkgsrv_SelectsPostgresStoreWhenConfigured()
    {
        var services = new ServiceCollection();
        services.AddPkgsrv(
            [],
            PkgsrvStorageOptions.Postgres(
                "Host=127.0.0.1;Port=1;Database=pkgsrv;Username=pkgsrv;Password=pkgsrv;Timeout=1"));

        using var provider = services.BuildServiceProvider();
        var repository = provider.GetRequiredService<IPackageRepository>();

        Assert.IsType<PostgresPackageRepository>(repository);
        Assert.Same(repository, provider.GetRequiredService<IPackageIndex>());
        Assert.NotNull(provider.GetRequiredService<PostgresPackageSchemaValidator>());
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
