using Microsoft.EntityFrameworkCore;
using Npgsql;

namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public interface IPackageSchemaMigrator
{
    Task MigrateAsync(CancellationToken cancellationToken);

    Task EnsureUpToDateAsync(CancellationToken cancellationToken);
}

public sealed class PostgresPackageSchemaMigrator : IPackageSchemaMigrator
{
    private readonly IDbContextFactory<PostgresPackageDbContext> _contextFactory;

    public PostgresPackageSchemaMigrator(
        IDbContextFactory<PostgresPackageDbContext> contextFactory)
    {
        ArgumentNullException.ThrowIfNull(contextFactory);
        _contextFactory = contextFactory;
    }

    public async Task MigrateAsync(CancellationToken cancellationToken)
    {
        await using var context = await _contextFactory
            .CreateDbContextAsync(cancellationToken)
            .ConfigureAwait(false);

        try
        {
            await context.Database
                .MigrateAsync(cancellationToken)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (NpgsqlException exception)
        {
            throw new InvalidOperationException(
                "PostgreSQL package storage is configured but EF Core migrations failed.",
                exception);
        }
    }

    public async Task EnsureUpToDateAsync(CancellationToken cancellationToken)
    {
        await using var context = await _contextFactory
            .CreateDbContextAsync(cancellationToken)
            .ConfigureAwait(false);

        try
        {
            var pending = (await context.Database
                .GetPendingMigrationsAsync(cancellationToken)
                .ConfigureAwait(false))
                .ToArray();

            if (pending.Length > 0)
            {
                throw new InvalidOperationException(
                    "PostgreSQL package storage has pending EF Core migrations: "
                    + string.Join(", ", pending)
                    + ". Set Pkgsrv:Database:InitializeSchema=true or run "
                    + "dotnet ef database update.");
            }
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (InvalidOperationException)
        {
            throw;
        }
        catch (NpgsqlException exception)
        {
            throw new InvalidOperationException(
                "PostgreSQL package storage is configured but the EF Core migration state "
                + "is unavailable. Apply migrations with Pkgsrv:Database:InitializeSchema=true "
                + "or dotnet ef database update.",
                exception);
        }
    }
}
