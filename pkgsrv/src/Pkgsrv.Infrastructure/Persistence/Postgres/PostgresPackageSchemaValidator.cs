using Microsoft.EntityFrameworkCore;
using Npgsql;

namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public interface IPackageSchemaValidator
{
    Task ValidateAsync(CancellationToken cancellationToken);
}

public sealed class PostgresPackageSchemaValidator : IPackageSchemaValidator
{
    private readonly IDbContextFactory<PostgresPackageDbContext> _contextFactory;

    public PostgresPackageSchemaValidator(
        IDbContextFactory<PostgresPackageDbContext> contextFactory)
    {
        ArgumentNullException.ThrowIfNull(contextFactory);
        _contextFactory = contextFactory;
    }

    public static void EnsureSupportedVersion(int installedVersion)
    {
        if (installedVersion != PostgresPackageSchema.RequiredVersion)
        {
            throw new InvalidOperationException(
                "PostgreSQL package storage schema version "
                + $"{installedVersion} is not supported; expected version "
                + $"{PostgresPackageSchema.RequiredVersion}. Apply the migrations under "
                + "pkgsrv/db/migrations in order.");
        }
    }

    public static void EnsureSupportedShape(IReadOnlyCollection<string> missingColumns)
    {
        ArgumentNullException.ThrowIfNull(missingColumns);

        if (missingColumns.Count > 0)
        {
            throw new InvalidOperationException(
                "PostgreSQL package storage schema is missing required columns: "
                + string.Join(", ", missingColumns)
                + ". Apply the migrations under pkgsrv/db/migrations in order.");
        }
    }

    public async Task ValidateAsync(CancellationToken cancellationToken)
    {
        try
        {
            await using var context = _contextFactory.CreateDbContext();
            var connection = context.Database.GetDbConnection();
            await connection.OpenAsync(cancellationToken).ConfigureAwait(false);

            await using var command = connection.CreateCommand();
            command.CommandText = PostgresPackageSchema.ValidationSql;

            await using var reader = await command
                .ExecuteReaderAsync(cancellationToken)
                .ConfigureAwait(false);

            if (!await reader.ReadAsync(cancellationToken).ConfigureAwait(false))
            {
                throw new InvalidOperationException(
                    "PostgreSQL package storage schema validation returned no result.");
            }

            if (!reader.GetBoolean(0))
            {
                throw new InvalidOperationException(
                    "PostgreSQL package storage is configured but the packages table is missing. "
                    + "Apply pkgsrv/db/migrations/0001_packages.sql.");
            }

            EnsureSupportedVersion(reader.GetInt32(1));

            var missingColumns = reader.GetString(2)
                .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
            EnsureSupportedShape(missingColumns);
        }
        catch (PostgresException exception) when (
            exception.SqlState is PostgresErrorCodes.UndefinedTable
                or PostgresErrorCodes.InvalidSchemaName)
        {
            throw new InvalidOperationException(
                "PostgreSQL package storage is configured but the package schema is missing. "
                + "Apply pkgsrv/db/migrations/0001_packages.sql.",
                exception);
        }
        catch (PostgresException exception)
        {
            throw new InvalidOperationException(
                "PostgreSQL package storage is configured but schema validation failed.",
                exception);
        }
        catch (NpgsqlException exception)
        {
            throw new InvalidOperationException(
                "PostgreSQL package storage is configured but the database is unavailable.",
                exception);
        }
    }
}
