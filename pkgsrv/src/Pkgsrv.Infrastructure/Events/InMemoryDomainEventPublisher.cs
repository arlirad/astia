using System.Collections.Concurrent;
using Pkgsrv.Application.Abstractions.Events;
using Pkgsrv.Domain.Events;

namespace Pkgsrv.Infrastructure.Events;

public sealed class InMemoryDomainEventPublisher : IDomainEventPublisher
{
    private readonly ConcurrentQueue<IDomainEvent> _events = new();

    public IReadOnlyCollection<IDomainEvent> Events => _events.ToArray();

    public Task PublishAsync(
        IReadOnlyCollection<IDomainEvent> domainEvents,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(domainEvents);

        foreach (var domainEvent in domainEvents)
        {
            _events.Enqueue(domainEvent);
        }

        return Task.CompletedTask;
    }
}
