using Microsoft.EntityFrameworkCore;

namespace Pkgsrv.Infrastructure.Persistence.Postgres;

internal sealed record PostgresSchemaState(
    bool Connected,
    int AppliedMigrationCount,
    bool IsCurrent);

internal interface IPostgresSchemaStateReader
{
    Task<PostgresSchemaState> ReadAsync(CancellationToken cancellationToken);
}

internal sealed class PostgresSchemaStateReader : IPostgresSchemaStateReader
{
    private readonly IDbContextFactory<PostgresPackageDbContext> _contextFactory;

    public PostgresSchemaStateReader(
        IDbContextFactory<PostgresPackageDbContext> contextFactory)
    {
        ArgumentNullException.ThrowIfNull(contextFactory);
        _contextFactory = contextFactory;
    }

    public async Task<PostgresSchemaState> ReadAsync(CancellationToken cancellationToken)
    {
        await using var context = await _contextFactory
            .CreateDbContextAsync(cancellationToken)
            .ConfigureAwait(false);

        var connected = await context.Database
            .CanConnectAsync(cancellationToken)
            .ConfigureAwait(false);

        if (!connected)
        {
            return new PostgresSchemaState(
                Connected: false,
                AppliedMigrationCount: 0,
                IsCurrent: false);
        }

        var knownMigrations = context.Database.GetMigrations().ToArray();
        var pendingMigrations = (await context.Database
            .GetPendingMigrationsAsync(cancellationToken)
            .ConfigureAwait(false))
            .ToArray();
        var appliedMigrations = (await context.Database
            .GetAppliedMigrationsAsync(cancellationToken)
            .ConfigureAwait(false))
            .ToArray();

        var known = knownMigrations.ToHashSet(StringComparer.Ordinal);
        var applied = appliedMigrations.ToHashSet(StringComparer.Ordinal);

        return new PostgresSchemaState(
            Connected: true,
            AppliedMigrationCount: appliedMigrations.Length,
            IsCurrent: known.Count > 0
                && pendingMigrations.Length == 0
                && known.SetEquals(applied));
    }
}
