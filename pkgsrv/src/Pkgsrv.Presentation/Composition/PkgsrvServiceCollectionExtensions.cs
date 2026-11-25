using Arlirad.Ervi.Mediator;
using Microsoft.EntityFrameworkCore;
using Pkgsrv.Application.Features.Packages.Register;
using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Application.Interfaces.Builds;
using Pkgsrv.Application.Interfaces.Events;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Interfaces.Sources;
using Pkgsrv.Application.Interfaces.Time;
using Pkgsrv.Infrastructure.Artifacts;
using Pkgsrv.Infrastructure.Builds;
using Pkgsrv.Infrastructure.Events;
using Pkgsrv.Infrastructure.Packages;
using Pkgsrv.Infrastructure.Persistence.Postgres;
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
        IEnumerable<string>? allowedSourceHosts = null,
        PkgsrvDatabaseOptions? databaseOptions = null)
    {
        ArgumentNullException.ThrowIfNull(services);

        databaseOptions ??= PkgsrvDatabaseOptions.InMemory;

        services.AddSingleton<ISourceAllowlist>(
            new ConfiguredSourceAllowlist(allowedSourceHosts ?? Array.Empty<string>()));
        services.AddSingleton(databaseOptions);

        if (databaseOptions.Provider == PkgsrvDatabaseProvider.Postgres)
        {
            services.AddDbContextFactory<PostgresPackageDbContext>(options =>
                options.UseNpgsql(databaseOptions.ConnectionString));
            services.AddSingleton<PostgresPackageRepository>();
            services.AddSingleton<IPackageRepository>(
                provider => provider.GetRequiredService<PostgresPackageRepository>());
            services.AddSingleton<IPackageIndex>(
                provider => provider.GetRequiredService<PostgresPackageRepository>());

            if (databaseOptions.InitializeSchema)
            {
                services.AddSingleton<IPackageSchemaInitializer, PostgresPackageSchemaInitializer>();
            }
        }
        else
        {
            services.AddSingleton<InMemoryPackageRepository>();
            services.AddSingleton<IPackageRepository>(
                provider => provider.GetRequiredService<InMemoryPackageRepository>());
            services.AddSingleton<IPackageIndex>(
                provider => provider.GetRequiredService<InMemoryPackageRepository>());
        }

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
}
