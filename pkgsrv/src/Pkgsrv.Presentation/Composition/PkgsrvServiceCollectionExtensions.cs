using Arlirad.Ervi.Mediator;
using Npgsql;
using Pkgsrv.Application.Abstractions.Artifacts;
using Pkgsrv.Application.Abstractions.Builds;
using Pkgsrv.Application.Abstractions.Caching;
using Pkgsrv.Application.Abstractions.Events;
using Pkgsrv.Application.Abstractions.Persistence;
using Pkgsrv.Application.Abstractions.Security;
using Pkgsrv.Application.Abstractions.Sources;
using Pkgsrv.Application.Abstractions.Time;
using Pkgsrv.Application.Caching;
using Pkgsrv.Application.Features.Packages.Register;
using Pkgsrv.Infrastructure.Artifacts;
using Pkgsrv.Infrastructure.Builds;
using Pkgsrv.Infrastructure.Events;
using Pkgsrv.Infrastructure.Packages;
using Pkgsrv.Infrastructure.Postgres;
using Pkgsrv.Infrastructure.Security;
using Pkgsrv.Infrastructure.Sources;
using Pkgsrv.Infrastructure.Time;
using Pkgsrv.Presentation.Configuration;
using Pkgsrv.Presentation.Security;

namespace Pkgsrv.Presentation.Composition;

public static class PkgsrvServiceCollectionExtensions
{
    public static IServiceCollection AddPkgsrv(
        this IServiceCollection services,
        IEnumerable<string>? allowedSourceHosts = null) =>
        services.AddPkgsrv(allowedSourceHosts, PkgsrvStorageOptions.DefaultInMemory);

    public static IServiceCollection AddPkgsrv(
        this IServiceCollection services,
        IEnumerable<string>? allowedSourceHosts,
        PkgsrvStorageOptions storage)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentNullException.ThrowIfNull(storage);

        services.AddSingleton<ISourceAllowlist>(
            new ConfiguredSourceAllowlist(allowedSourceHosts ?? Array.Empty<string>()));
        AddPackageStore(services, storage);
        services.AddSingleton<IPackageReadModelCache>(provider =>
            new JsonPackageReadModelCache(
                storage.Cache,
                provider.GetRequiredService<ISystemClock>()));
        services.AddSingleton<IBuildSandbox, NonExecutingBuildSandbox>();
        services.AddSingleton<IArtifactMetadataStore, InMemoryArtifactMetadataStore>();
        services.AddSingleton<IPackageSigner, PendingPackageSigner>();
        services.AddSingleton<IPackagePublisher, RefusingUnsignedPackagePublisher>();
        services.AddSingleton<IDomainEventPublisher, InMemoryDomainEventPublisher>();
        services.AddSingleton<ISystemClock, SystemClock>();
        services.AddSingleton<IIrvaClientAuthorizationBoundary, PendingIrvaClientAuthorizationBoundary>();
        services.AddSingleton<IIrvaAuthorization, PendingIrvaClientAuthorizationAdapter>();

        services.AddHttpContextAccessor();
        services.AddScoped<IActorContextAccessor, HeaderActorContextAccessor>();

        services.AddMediator(typeof(RegisterPackageCommandHandler));

        return services;
    }

    private static void AddPackageStore(
        IServiceCollection services,
        PkgsrvStorageOptions storage)
    {
        if (storage.Provider == PkgsrvStorageProvider.Postgres)
        {
            var options = new PostgresPackageStoreOptions(
                storage.PostgresConnectionString!,
                storage.MaxListResults);

            services.AddSingleton(options);
            services.AddSingleton<NpgsqlDataSource>(_ =>
                NpgsqlDataSource.Create(options.ConnectionString));
            services.AddSingleton<PostgresPackageRepository>();
            services.AddSingleton<IPackageRepository>(provider =>
                provider.GetRequiredService<PostgresPackageRepository>());
            services.AddSingleton<IPackageIndex>(provider =>
                provider.GetRequiredService<PostgresPackageRepository>());
            services.AddSingleton<PostgresPackageSchemaValidator>();

            return;
        }

        services.AddSingleton<InMemoryPackageRepository>();
        services.AddSingleton<IPackageRepository>(provider =>
            provider.GetRequiredService<InMemoryPackageRepository>());
        services.AddSingleton<IPackageIndex>(provider =>
            provider.GetRequiredService<InMemoryPackageRepository>());
    }
}
