using Microsoft.EntityFrameworkCore;
using Pkgsrv.Application.Interfaces.Persistence;

namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public sealed class PostgresStorageReadinessProbe : IStorageReadinessProbe
{
    private readonly IPostgresSchemaStateReader _stateReader;
    private readonly TimeSpan _timeout;

    public PostgresStorageReadinessProbe(
        IDbContextFactory<PostgresPackageDbContext> contextFactory,
        TimeSpan timeout)
        : this(new PostgresSchemaStateReader(contextFactory), timeout)
    {
    }

    internal PostgresStorageReadinessProbe(
        IPostgresSchemaStateReader stateReader,
        TimeSpan timeout)
    {
        ArgumentNullException.ThrowIfNull(stateReader);

        if (timeout <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(timeout), "Readiness timeout must be positive.");
        }

        _stateReader = stateReader;
        _timeout = timeout;
    }

    public async Task<StorageReadinessReport> ProbeAsync(CancellationToken cancellationToken)
    {
        using var timeoutSource =
            CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(_timeout);

        try
        {
            var state = await _stateReader
                .ReadAsync(timeoutSource.Token)
                .ConfigureAwait(false);

            if (!state.Connected)
            {
                return NotReady(schemaVersion: null);
            }

            return new StorageReadinessReport(
                IsReady: state.IsCurrent,
                Provider: StorageProviderKind.Postgres,
                IsDurable: true,
                SchemaVersion: state.AppliedMigrationCount,
                PostgresAvailable: true);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            return NotReady(schemaVersion: null);
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
            return NotReady(schemaVersion: null);
        }
    }

    private static StorageReadinessReport NotReady(int? schemaVersion) =>
        new(
            IsReady: false,
            Provider: StorageProviderKind.Postgres,
            IsDurable: true,
            SchemaVersion: schemaVersion,
            PostgresAvailable: false);
}
