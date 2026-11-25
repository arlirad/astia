using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Interfaces.Persistence;

public interface IPackageRepository
{
    Task<Package?> FindAsync(PackageKey key, CancellationToken cancellationToken);

    Task<bool> TryAddAsync(Package package, CancellationToken cancellationToken);

    Task UpdateAsync(Package package, CancellationToken cancellationToken);

    Task AcknowledgeDomainEventsAsync(
        PackageKey key,
        IReadOnlyCollection<IDomainEvent> publishedEvents,
        CancellationToken cancellationToken);
}

public sealed record PackageIndexEntry(
    PackageManifest Manifest,
    PackageState State,
    DateTimeOffset RegisteredAt);

public interface IPackageIndex
{
    Task<IReadOnlyList<PackageIndexEntry>> SearchAsync(
        string? search,
        CancellationToken cancellationToken);
}
