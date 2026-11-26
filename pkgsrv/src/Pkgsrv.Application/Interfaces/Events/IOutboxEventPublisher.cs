using Pkgsrv.Domain.Events;

namespace Pkgsrv.Application.Interfaces.Events;

public sealed record OutboxEventMessage(
    string IdempotencyKey,
    string EventType,
    DateTimeOffset OccurredAt,
    IReadOnlyCollection<IDomainEvent> Events);

public interface IOutboxEventPublisher
{
    Task PublishAsync(
        OutboxEventMessage message,
        CancellationToken cancellationToken);
}
