using Pkgsrv.Domain.Events;

namespace Pkgsrv.Application.Interfaces.Events;

public interface IDomainEventPublisher
{
    Task PublishAsync(
        IReadOnlyCollection<IDomainEvent> domainEvents,
        CancellationToken cancellationToken);
}
