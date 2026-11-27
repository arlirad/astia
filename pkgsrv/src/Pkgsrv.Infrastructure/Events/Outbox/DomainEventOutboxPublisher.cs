using Microsoft.Extensions.Logging;
using Pkgsrv.Application.Interfaces.Events;

namespace Pkgsrv.Infrastructure.Events.Outbox;

public sealed class DomainEventOutboxPublisher : IOutboxEventPublisher
{
    private readonly IDomainEventPublisher _publisher;
    private readonly ILogger<DomainEventOutboxPublisher> _logger;

    public DomainEventOutboxPublisher(
        IDomainEventPublisher publisher,
        ILogger<DomainEventOutboxPublisher> logger)
    {
        _publisher = publisher;
        _logger = logger;
    }

    public async Task PublishAsync(
        OutboxEventMessage message,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(message);

        _logger.LogInformation(
            "Publishing package outbox event {EventType} with idempotency key {IdempotencyKey}.",
            message.EventType,
            message.IdempotencyKey);

        await _publisher
            .PublishAsync(message.Events, cancellationToken)
            .ConfigureAwait(false);
    }
}
