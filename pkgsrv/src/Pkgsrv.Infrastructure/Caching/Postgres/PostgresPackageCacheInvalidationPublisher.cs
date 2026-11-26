using Npgsql;
using Pkgsrv.Application.Interfaces.Caching;

namespace Pkgsrv.Infrastructure.Caching.Postgres;

public sealed class PostgresPackageCacheInvalidationPublisher
    : IPackageCacheInvalidationPublisher
{
    private static readonly TimeSpan PublishTimeout = TimeSpan.FromSeconds(2);

    private readonly NpgsqlDataSource _dataSource;

    public PostgresPackageCacheInvalidationPublisher(NpgsqlDataSource dataSource)
    {
        _dataSource = dataSource;
    }

    public async Task PublishAsync(
        PackageCacheInvalidation invalidation,
        CancellationToken cancellationToken)
    {
        var payload = PostgresPackageCacheInvalidationChannel.Serialize(invalidation);

        using var timeoutSource =
            CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(PublishTimeout);

        await using var connection = await _dataSource
            .OpenConnectionAsync(timeoutSource.Token)
            .ConfigureAwait(false);
        await using var command = new NpgsqlCommand(
            PostgresPackageCacheInvalidationChannel.NotifyCommand,
            connection);
        command.Parameters.AddWithValue(
            "channel",
            PostgresPackageCacheInvalidationChannel.ChannelName);
        command.Parameters.AddWithValue("payload", payload);

        await command.ExecuteNonQueryAsync(timeoutSource.Token).ConfigureAwait(false);
    }
}
