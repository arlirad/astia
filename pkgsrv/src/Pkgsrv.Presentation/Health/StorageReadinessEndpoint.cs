using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Presentation.Configuration;

namespace Pkgsrv.Presentation.Health;

public static class StorageReadinessEndpoint
{
    public const string Ready = "ready";
    public const string NotReady = "not_ready";

    public static async Task<IResult> HandleAsync(
        IServiceProvider services,
        PkgsrvDatabaseProvider configuredProvider,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(services);

        IStorageReadinessProbe? probe;

        try
        {
            probe = services.GetService<IStorageReadinessProbe>();
        }
        catch (Exception)
        {
            probe = null;
        }

        var report = probe is null
            ? CreateFallback(configuredProvider)
            : await ProbeSafelyAsync(probe, configuredProvider, cancellationToken)
                .ConfigureAwait(false);

        return Results.Json(
            new
            {
                status = report.IsReady ? Ready : NotReady,
                storageProvider = ToProviderName(report.Provider),
                storageDurable = report.IsDurable,
                schemaVersion = report.SchemaVersion,
                postgresAvailable = report.PostgresAvailable
            },
            statusCode: report.IsReady
                ? StatusCodes.Status200OK
                : StatusCodes.Status503ServiceUnavailable);
    }

    private static async Task<StorageReadinessReport> ProbeSafelyAsync(
        IStorageReadinessProbe probe,
        PkgsrvDatabaseProvider configuredProvider,
        CancellationToken cancellationToken)
    {
        try
        {
            return await probe.ProbeAsync(cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (Exception)
        {
            return CreateFallback(configuredProvider);
        }
    }

    private static StorageReadinessReport CreateFallback(PkgsrvDatabaseProvider provider) =>
        provider == PkgsrvDatabaseProvider.InMemory
            ? new StorageReadinessReport(
                IsReady: true,
                Provider: StorageProviderKind.InMemory,
                IsDurable: false,
                SchemaVersion: null,
                PostgresAvailable: null)
            : new StorageReadinessReport(
                IsReady: false,
                Provider: StorageProviderKind.Postgres,
                IsDurable: true,
                SchemaVersion: null,
                PostgresAvailable: false);

    private static string ToProviderName(StorageProviderKind provider) =>
        provider switch
        {
            StorageProviderKind.InMemory => "InMemory",
            StorageProviderKind.Postgres => "Postgres",
            _ => "Unknown"
        };
}
