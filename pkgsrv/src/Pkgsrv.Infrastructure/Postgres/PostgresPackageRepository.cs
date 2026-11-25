using System.Globalization;
using Npgsql;
using NpgsqlTypes;
using Pkgsrv.Application.Abstractions.Persistence;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Postgres;

public sealed class PostgresPackageRepository : IPackageRepository, IPackageIndex
{
    private readonly NpgsqlDataSource _dataSource;
    private readonly PostgresPackageStoreOptions _options;

    public PostgresPackageRepository(
        NpgsqlDataSource dataSource,
        PostgresPackageStoreOptions options)
    {
        ArgumentNullException.ThrowIfNull(dataSource);
        ArgumentNullException.ThrowIfNull(options);

        _dataSource = dataSource;
        _options = options;
    }

    public async Task<Package?> FindAsync(PackageKey key, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(key);
        cancellationToken.ThrowIfCancellationRequested();

        await using var command = _dataSource.CreateCommand(PostgresPackageSql.Find);
        command.Parameters.Add(new NpgsqlParameter("name", NpgsqlDbType.Text) { Value = key.Name.Value });
        command.Parameters.Add(new NpgsqlParameter("version", NpgsqlDbType.Text) { Value = key.Version.ToString() });

        await using var reader = await command
            .ExecuteReaderAsync(cancellationToken)
            .ConfigureAwait(false);

        if (!await reader.ReadAsync(cancellationToken).ConfigureAwait(false))
        {
            return null;
        }

        return PostgresPackageRow.Read(reader).ToPackage();
    }

    public async Task<bool> TryAddAsync(Package package, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(package);
        cancellationToken.ThrowIfCancellationRequested();

        var row = PostgresPackageRow.FromPackage(package);

        await using var command = _dataSource.CreateCommand(PostgresPackageSql.Insert);
        row.AddInsertParameters(command);

        var affected = await command
            .ExecuteNonQueryAsync(cancellationToken)
            .ConfigureAwait(false);

        return affected == 1;
    }

    public async Task UpdateAsync(Package package, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(package);
        cancellationToken.ThrowIfCancellationRequested();

        var row = PostgresPackageRow.FromPackage(package);

        await using var connection = await _dataSource
            .OpenConnectionAsync(cancellationToken)
            .ConfigureAwait(false);
        await using var transaction = await connection
            .BeginTransactionAsync(cancellationToken)
            .ConfigureAwait(false);

        await using (var select = new NpgsqlCommand(
            PostgresPackageSql.SelectStateForUpdate,
            connection,
            transaction))
        {
            row.AddIdentityParameters(select);
            var currentState = await select
                .ExecuteScalarAsync(cancellationToken)
                .ConfigureAwait(false);

            if (currentState is null or DBNull)
            {
                throw new InvalidOperationException(
                    $"Package {package.Key} does not exist in the store.");
            }

            var persistedState = (PackageState)Convert.ToInt16(currentState, CultureInfo.InvariantCulture);
            PostgresPackageStateTransition.EnsureAllowed(persistedState, package.State);
        }

        await using (var update = new NpgsqlCommand(
            PostgresPackageSql.UpdateState,
            connection,
            transaction))
        {
            row.AddIdentityParameters(update);
            row.AddStateParameters(update);
            await update
                .ExecuteNonQueryAsync(cancellationToken)
                .ConfigureAwait(false);
        }

        await transaction.CommitAsync(cancellationToken).ConfigureAwait(false);
    }

    public async Task<IReadOnlyList<PackageIndexEntry>> SearchAsync(
        string? search,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        await using var command = _dataSource.CreateCommand(PostgresPackageSql.Search);
        PostgresPackageSearch.BindParameters(command, search, _options.MaxListResults);

        await using var reader = await command
            .ExecuteReaderAsync(cancellationToken)
            .ConfigureAwait(false);

        var entries = new List<PackageIndexEntry>();

        while (await reader.ReadAsync(cancellationToken).ConfigureAwait(false))
        {
            var package = PostgresPackageRow.Read(reader).ToPackage();
            entries.Add(new PackageIndexEntry(
                package.Manifest,
                package.State,
                package.RegisteredAt));
        }

        return entries;
    }
}
