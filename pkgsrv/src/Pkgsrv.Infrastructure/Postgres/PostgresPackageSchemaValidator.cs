using System.Globalization;
using Npgsql;

namespace Pkgsrv.Infrastructure.Postgres;

public sealed class PostgresPackageSchemaValidator
{
    private readonly NpgsqlDataSource _dataSource;

    public PostgresPackageSchemaValidator(NpgsqlDataSource dataSource)
    {
        ArgumentNullException.ThrowIfNull(dataSource);
        _dataSource = dataSource;
    }

    public static void EnsureSupportedVersion(int installedVersion)
    {
        if (installedVersion != PostgresPackageSql.RequiredSchemaVersion)
        {
            throw new InvalidOperationException(
                "PostgreSQL package storage schema version "
                + $"{installedVersion} is not supported; expected version "
                + $"{PostgresPackageSql.RequiredSchemaVersion}. Apply the matching migration "
                + "under pkgsrv/db/migrations.");
        }
    }

    public async Task ValidateAsync(CancellationToken cancellationToken)
    {
        try
        {
            await using (var schemaCommand = _dataSource.CreateCommand(PostgresPackageSql.SchemaCheck))
            {
                var packageTable = await schemaCommand
                    .ExecuteScalarAsync(cancellationToken)
                    .ConfigureAwait(false);

                if (packageTable is null or DBNull)
                {
                    throw new InvalidOperationException(
                        "PostgreSQL package storage is configured but the package table is missing. "
                        + "Apply pkgsrv/db/migrations/0001_packages.sql.");
                }
            }

            await using var command = _dataSource.CreateCommand(PostgresPackageSql.SchemaVersion);
            var value = await command
                .ExecuteScalarAsync(cancellationToken)
                .ConfigureAwait(false);

            if (value is null or DBNull)
            {
                throw new InvalidOperationException(
                    "PostgreSQL package storage is configured but the schema version is not installed. "
                    + "Apply pkgsrv/db/migrations/0001_packages.sql.");
            }

            var installedVersion = Convert.ToInt32(value, CultureInfo.InvariantCulture);
            EnsureSupportedVersion(installedVersion);
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
        catch (NpgsqlException exception)
        {
            throw new InvalidOperationException(
                "PostgreSQL package storage is configured but the database is unavailable.",
                exception);
        }
    }
}
