using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Npgsql;
using Pkgsrv.Application.Interfaces.Caching;

namespace Pkgsrv.Infrastructure.Caching.Postgres;

public sealed class PostgresPackageCacheInvalidationListener : BackgroundService
{
    private static readonly TimeSpan MinimumBackoff = TimeSpan.FromSeconds(1);
    private static readonly TimeSpan MaximumBackoff = TimeSpan.FromSeconds(30);

    private readonly NpgsqlDataSource _dataSource;
    private readonly IPackageCacheInvalidator _invalidator;
    private readonly ILogger<PostgresPackageCacheInvalidationListener> _logger;

    public PostgresPackageCacheInvalidationListener(
        NpgsqlDataSource dataSource,
        IPackageCacheInvalidator invalidator,
        ILogger<PostgresPackageCacheInvalidationListener> logger)
    {
        _dataSource = dataSource;
        _invalidator = invalidator;
        _logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        var backoff = MinimumBackoff;

        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                await ListenAsync(stoppingToken).ConfigureAwait(false);
                backoff = MinimumBackoff;
            }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
            {
                break;
            }
            catch (Exception exception)
            {
                _logger.LogWarning(
                    "Package cache invalidation listener disconnected ({ErrorType}); retrying in {DelaySeconds} seconds.",
                    exception.GetType().Name,
                    backoff.TotalSeconds);

                try
                {
                    await Task.Delay(backoff, stoppingToken).ConfigureAwait(false);
                }
                catch (OperationCanceledException)
                {
                    break;
                }

                backoff = TimeSpan.FromSeconds(
                    Math.Min(backoff.TotalSeconds * 2, MaximumBackoff.TotalSeconds));
            }
        }
    }

    private async Task ListenAsync(CancellationToken cancellationToken)
    {
        await using var connection = await _dataSource
            .OpenConnectionAsync(cancellationToken)
            .ConfigureAwait(false);
        connection.Notification += (_, eventArgs) => Handle(eventArgs.Payload);

        await using (var command = new NpgsqlCommand(
            PostgresPackageCacheInvalidationChannel.ListenCommand,
            connection))
        {
            await command.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);
        }

        _logger.LogInformation(
            "Listening for package cache invalidations on channel {Channel}.",
            PostgresPackageCacheInvalidationChannel.ChannelName);

        while (!cancellationToken.IsCancellationRequested)
        {
            await connection.WaitAsync(cancellationToken).ConfigureAwait(false);
        }
    }

    private void Handle(string payload)
    {
        if (!PostgresPackageCacheInvalidationChannel.TryParse(
                payload,
                out var invalidation))
        {
            _logger.LogWarning(
                "Ignoring malformed package cache invalidation notification on channel {Channel}.",
                PostgresPackageCacheInvalidationChannel.ChannelName);
            return;
        }

        try
        {
            _invalidator.Invalidate(invalidation);
        }
        catch (Exception exception)
        {
            _logger.LogWarning(
                "Local package cache invalidation failed for scope {Scope} ({ErrorType}).",
                invalidation.Scope,
                exception.GetType().Name);
        }
    }
}
