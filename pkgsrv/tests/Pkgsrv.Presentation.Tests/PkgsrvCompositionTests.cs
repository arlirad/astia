using Arlirad.Ervi.Mediator.Interfaces;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Pkgsrv.Application.Interfaces.Builds;
using Pkgsrv.Application.Interfaces.Events;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Features.Packages.List;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Events.Outbox;
using Pkgsrv.Infrastructure.Packages;
using Pkgsrv.Infrastructure.Persistence;
using Pkgsrv.Infrastructure.Persistence.Postgres;
using Pkgsrv.Infrastructure.Security;
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
        Assert.Equal(AuthorizationFailure.AuthenticationRequired, decision.Failure);

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
        Assert.IsType<InMemoryStorageReadinessProbe>(
            provider.GetRequiredService<IStorageReadinessProbe>());
        Assert.Null(provider.GetService<IPackageEventOutbox>());
        Assert.Null(provider.GetService<IPackageOutboxStore>());
        Assert.Null(provider.GetService<IOutboxEventPublisher>());
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
        services.AddLogging();
        services.AddPkgsrv([], databaseOptions);

        using var provider = services.BuildServiceProvider();

        Assert.IsType<PostgresPackageRepository>(
            provider.GetRequiredService<IPackageRepository>());
        Assert.IsType<PostgresPackageRepository>(
            provider.GetRequiredService<IPackageIndex>());
        Assert.NotNull(provider.GetRequiredService<IPackageSchemaMigrator>());
        Assert.IsType<PostgresStorageReadinessProbe>(
            provider.GetRequiredService<IStorageReadinessProbe>());
        Assert.Same(
            provider.GetRequiredService<PostgresPackageRepository>(),
            provider.GetRequiredService<IPackageEventOutbox>());
        Assert.IsType<PostgresPackageOutboxStore>(
            provider.GetRequiredService<IPackageOutboxStore>());
        Assert.IsType<DomainEventOutboxPublisher>(
            provider.GetRequiredService<IOutboxEventPublisher>());
        Assert.Contains(
            services,
            descriptor => descriptor.ImplementationType == typeof(PackageOutboxDispatcher));
    }

    [DefaultIrvaBuildFact]
    public async Task AddPkgsrv_EnabledWithoutWiredIrvaDeniesWithToken()
    {
        var services = new ServiceCollection();
        services.AddPkgsrv([], irvaOptions: CreateIrvaOptions(required: false));

        await using var provider = services.BuildServiceProvider();
        var httpContextAccessor = provider.GetRequiredService<IHttpContextAccessor>();
        var httpContext = new DefaultHttpContext();
        httpContext.Request.Headers.Authorization = "Bearer access-token";
        httpContextAccessor.HttpContext = httpContext;

        var authorization = provider.GetRequiredService<IIrvaAuthorization>();
        var decision = await authorization.AuthorizeAsync(
            TestPackages.Actor,
            PackageAction.Read,
            null,
            CancellationToken.None);

        Assert.False(decision.IsAllowed);
        Assert.Equal(AuthorizationFailure.Unavailable, decision.Failure);
        Assert.Contains("not supplied at build time", decision.Reason, StringComparison.Ordinal);
    }

    [DefaultIrvaBuildFact]
    public void AddPkgsrv_EnabledWithoutWiredIrvaAndRequiredFailsComposition()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<InvalidOperationException>(
            () => services.AddPkgsrv([], irvaOptions: CreateIrvaOptions(required: true)));

        Assert.Contains("Irva.Client project", exception.Message, StringComparison.Ordinal);
    }

    [RealIrvaBuildFact]
    public void AddPkgsrv_RealIrvaBuildReportsAdapterAvailable()
    {
        Assert.True(IrvaClientAdapterFactory.IsAvailable);
    }

    [RealIrvaBuildFact]
    public void AddPkgsrv_RealIrvaBuildComposesEnabledIrvaAndRequired()
    {
        var services = new ServiceCollection();

        services.AddPkgsrv([], irvaOptions: CreateIrvaOptions(required: true));

        using var provider = services.BuildServiceProvider();
        var adapter = provider.GetRequiredService<IIrvaClientAdapter>();

        Assert.IsNotType<UnavailableIrvaClientAdapter>(adapter);
        Assert.Equal("RealIrvaClientAdapter", adapter.GetType().Name);
        Assert.False(adapter.HasPermission(Guid.NewGuid(), "read"));
        Assert.IsType<IrvaClientIdentityVerifier>(
            provider.GetRequiredService<IIrvaIdentityVerifier>());
        Assert.IsType<IrvaClientAuthorizationBoundary>(
            provider.GetRequiredService<IIrvaClientAuthorizationBoundary>());
    }

    [Fact]
    public void AddPkgsrv_EnabledIncompleteAndRequiredFailsComposition()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<InvalidOperationException>(
            () => services.AddPkgsrv(
                [],
                irvaOptions: CreateIrvaOptions(required: true, includeSettings: false)));

        Assert.Contains("BaseUrl", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task AddPkgsrv_DisabledWithBearerReturnsUnavailable()
    {
        var services = new ServiceCollection();
        services.AddPkgsrv([]);

        await using var provider = services.BuildServiceProvider();
        var httpContextAccessor = provider.GetRequiredService<IHttpContextAccessor>();
        var httpContext = new DefaultHttpContext();
        httpContext.Request.Headers.Authorization = "Bearer access-token";
        httpContextAccessor.HttpContext = httpContext;

        var authorization = provider.GetRequiredService<IIrvaAuthorization>();
        var decision = await authorization.AuthorizeAsync(
            TestPackages.Actor,
            PackageAction.Read,
            null,
            CancellationToken.None);

        Assert.False(decision.IsAllowed);
        Assert.Equal(AuthorizationFailure.Unavailable, decision.Failure);
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

    private static PkgsrvIrvaOptions CreateIrvaOptions(
        bool required,
        bool includeSettings = true)
    {
        var values = new Dictionary<string, string?>
        {
            ["Pkgsrv:Irva:Enabled"] = "true",
            ["Pkgsrv:Irva:Required"] = required.ToString()
        };

        if (includeSettings)
        {
            values["Pkgsrv:Irva:BaseUrl"] = "https://irva.example";
            values["Pkgsrv:Irva:AppToken"] = "configured-secret";
        }

        var configuration = new ConfigurationBuilder()
            .AddInMemoryCollection(values)
            .Build();

        return PkgsrvIrvaOptions.FromConfiguration(configuration);
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
