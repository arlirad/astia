using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Pkgsrv.Application.Interfaces.Events;
using Pkgsrv.Application.Interfaces.Time;
using Pkgsrv.Infrastructure.Persistence.Postgres;

namespace Pkgsrv.Infrastructure.Events.Outbox;

public sealed class PackageOutboxDispatcher : BackgroundService
{
    private readonly IPackageOutboxStore _store;
    private readonly IOutboxEventPublisher _publisher;
    private readonly ISystemClock _clock;
    private readonly PackageOutboxDispatcherOptions _options;
    private readonly ILogger<PackageOutboxDispatcher> _logger;

    public PackageOutboxDispatcher(
        IPackageOutboxStore store,
        IOutboxEventPublisher publisher,
        ISystemClock clock,
        PackageOutboxDispatcherOptions options,
        ILogger<PackageOutboxDispatcher> logger)
    {
        _store = store;
        _publisher = publisher;
        _clock = clock;
        _options = options;
        _logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                var dispatched = await DispatchPendingAsync(stoppingToken).ConfigureAwait(false);

                if (dispatched == 0)
                {
                    await Task.Delay(_options.PollInterval, stoppingToken).ConfigureAwait(false);
                }
            }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
            {
                break;
            }
            catch (Exception exception)
            {
                _logger.LogError(
                    exception,
                    "Package outbox dispatcher iteration failed ({ErrorType}).",
                    exception.GetType().Name);
                await Task.Delay(_options.ErrorBackoff, stoppingToken).ConfigureAwait(false);
            }
        }
    }

    internal async Task<int> DispatchPendingAsync(CancellationToken cancellationToken)
    {
        var messages = await _store
            .ClaimAsync(
                _clock.UtcNow,
                _options.BatchSize,
                _options.LeaseDuration,
                cancellationToken)
            .ConfigureAwait(false);

        foreach (var message in messages)
        {
            await DispatchAsync(message, cancellationToken).ConfigureAwait(false);
        }

        return messages.Count;
    }

    internal async Task<bool> DispatchAsync(
        PackageOutboxMessage message,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(message);

        try
        {
            var events = PostgresOutboxEventSerializer.Deserialize(
                message.EventType,
                message.Payload);

            await _publisher
                .PublishAsync(
                    new OutboxEventMessage(
                        message.IdempotencyKey,
                        message.EventType,
                        message.OccurredAt,
                        events),
                    cancellationToken)
                .ConfigureAwait(false);

            await _store
                .MarkDeliveredAsync(message.IdempotencyKey, message.ClaimId, cancellationToken)
                .ConfigureAwait(false);
            return true;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (Exception exception)
        {
            var exhausted = !OutboxRetryPolicy.ShouldRetry(
                message.AttemptCount,
                _options.MaxAttempts);
            var now = _clock.UtcNow;
            var nextAvailableAt = exhausted
                ? now
                : now + OutboxRetryPolicy.Backoff(
                    message.AttemptCount,
                    _options.BaseBackoff,
                    _options.MaxBackoff);

            _logger.LogWarning(
                exception,
                "Package outbox event {EventType} with idempotency key {IdempotencyKey} failed on attempt {AttemptCount}.",
                message.EventType,
                message.IdempotencyKey,
                message.AttemptCount);

            try
            {
                await _store
                    .MarkFailedAsync(
                        message.IdempotencyKey,
                        message.ClaimId,
                        message.AttemptCount,
                        nextAvailableAt,
                        exhausted,
                        exception.GetType().Name,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                throw;
            }
            catch (Exception markException)
            {
                _logger.LogError(
                    markException,
                    "Package outbox failure state could not be persisted for idempotency key {IdempotencyKey} ({ErrorType}).",
                    message.IdempotencyKey,
                    markException.GetType().Name);
            }

            return false;
        }
    }
}
