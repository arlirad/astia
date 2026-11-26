using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Interfaces.Persistence;

public interface IPackageRepository
{
    Task<Package?> FindAsync(PackageKey key, CancellationToken cancellationToken);

    Task<bool> TryAddAsync(Package package, CancellationToken cancellationToken);

    Task UpdateAsync(Package package, CancellationToken cancellationToken);

    Task UpdateSignatureAsync(Package package, CancellationToken cancellationToken);

    Task MarkPublishedAsync(
        Package package,
        DateTimeOffset publishedAt,
        CancellationToken cancellationToken);

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
    /// <summary>
    /// Searches indexed fields only: package name contains and source commit prefix,
    /// both case-insensitive. Provenance and other metadata are not search fields.
    /// A null or whitespace value returns the full bounded, deterministically ordered list.
    /// </summary>
    Task<IReadOnlyList<PackageIndexEntry>> SearchAsync(
        string? search,
        CancellationToken cancellationToken);
}
