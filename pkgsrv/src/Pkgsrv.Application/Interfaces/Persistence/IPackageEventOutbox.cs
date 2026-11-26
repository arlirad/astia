using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Interfaces.Persistence;

public interface IPackageEventOutbox
{
    Task<bool> TryAddWithEventsAsync(
        Package package,
        IReadOnlyCollection<IDomainEvent> domainEvents,
        CancellationToken cancellationToken);

    Task UpdateWithEventsAsync(
        Package package,
        IReadOnlyCollection<IDomainEvent> domainEvents,
        CancellationToken cancellationToken);
}
