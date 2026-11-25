using Pkgsrv.Domain.Events;

namespace Pkgsrv.Application.Abstractions.Events;

public interface IDomainEventPublisher
{
    Task PublishAsync(
        IReadOnlyCollection<IDomainEvent> domainEvents,
        CancellationToken cancellationToken);
}
