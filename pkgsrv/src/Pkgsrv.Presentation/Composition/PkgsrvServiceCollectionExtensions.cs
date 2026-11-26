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
        PkgsrvDatabaseOptions? databaseOptions = null,
        PkgsrvIrvaOptions? irvaOptions = null)
    {
        ArgumentNullException.ThrowIfNull(services);

        databaseOptions ??= PkgsrvDatabaseOptions.InMemory;
        irvaOptions ??= PkgsrvIrvaOptions.Disabled;

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
            services.AddSingleton<IPackageSchemaMigrator, PostgresPackageSchemaMigrator>();
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
        services.AddSingleton<IArtifactStore, InMemoryArtifactStore>();
        services.AddSingleton<IArtifactMetadataStore, InMemoryArtifactMetadataStore>();
        services.AddSingleton<IPackageSigner, PendingPackageSigner>();
        services.AddSingleton<IPackageSignatureVerifier, RefusingPackageSignatureVerifier>();
        services.AddSingleton<IPackagePublisher, RefusingUnsignedPackagePublisher>();
        services.AddSingleton<IDomainEventPublisher, InMemoryDomainEventPublisher>();
        services.AddSingleton<ISystemClock, SystemClock>();
        services.AddSingleton(irvaOptions);

        services.AddHttpContextAccessor();
        services.AddSingleton<HttpTransportIdentityAccessor>();
        services.AddSingleton<ITransportIdentityAccessor>(
            provider => provider.GetRequiredService<HttpTransportIdentityAccessor>());
        services.AddSingleton<IActorContextAccessor>(
            provider => provider.GetRequiredService<HttpTransportIdentityAccessor>());
        AddIrvaAuthorization(services, irvaOptions);

        services.AddMediator(typeof(RegisterPackageCommandHandler));

        return services;
    }

    private static void AddIrvaAuthorization(
        IServiceCollection services,
        PkgsrvIrvaOptions options)
    {
        if (!options.Enabled)
        {
            services.AddSingleton<IIrvaIdentityVerifier, PendingIrvaIdentityVerifier>();
            services.AddSingleton<IIrvaClientAuthorizationBoundary, PendingIrvaClientAuthorizationBoundary>();
            services.AddSingleton<IIrvaAuthorization, IrvaClientAuthorizationAdapter>();
            return;
        }

        IIrvaClientAdapter adapter;

        if (!options.IsComplete)
        {
            const string reason =
                "Pkgsrv:Irva:BaseUrl and Pkgsrv:Irva:AppToken are required for the Irva.Client integration.";

            if (options.Required)
            {
                throw new InvalidOperationException(reason);
            }

            adapter = new UnavailableIrvaClientAdapter(reason);
        }
        else if (!IrvaClientAdapterFactory.IsAvailable)
        {
            const string reason =
                "The Irva.Client project was not supplied at build time. Build with -p:IrvaClientProject=<path-to-Irva.Client.csproj>.";

            if (options.Required)
            {
                throw new InvalidOperationException(reason);
            }

            adapter = new UnavailableIrvaClientAdapter(reason);
        }
        else
        {
            adapter = IrvaClientAdapterFactory.Create(options.ToConnectionSettings());
        }

        services.AddSingleton(adapter);
        services.AddSingleton<IIrvaIdentityVerifier, IrvaClientIdentityVerifier>();
        services.AddSingleton<IIrvaClientAuthorizationBoundary, IrvaClientAuthorizationBoundary>();
        services.AddSingleton<IIrvaAuthorization, IrvaClientAuthorizationAdapter>();
    }
}
