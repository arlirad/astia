using System.Collections.Concurrent;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Packages;

public sealed class InMemoryPackageRepository : IPackageRepository, IPackageIndex
{
    private readonly ConcurrentDictionary<PackageKey, Package> _packages = new();

    public Task<Package?> FindAsync(PackageKey key, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(key);
        _packages.TryGetValue(key, out var package);
        return Task.FromResult(package);
    }

    public Task<bool> TryAddAsync(Package package, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(package);
        return Task.FromResult(_packages.TryAdd(package.Key, package));
    }

    public Task UpdateAsync(Package package, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(package);

        if (!_packages.ContainsKey(package.Key))
        {
            throw new InvalidOperationException($"Package {package.Key} does not exist in the store.");
        }

        _packages[package.Key] = package;
        return Task.CompletedTask;
    }

    public Task UpdateSignatureAsync(Package package, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(package);

        if (!_packages.ContainsKey(package.Key))
        {
            throw new InvalidOperationException($"Package {package.Key} does not exist in the store.");
        }

        _packages[package.Key] = package;
        return Task.CompletedTask;
    }

    public Task MarkPublishedAsync(
        Package package,
        DateTimeOffset publishedAt,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(package);

        if (!_packages.ContainsKey(package.Key))
        {
            throw new InvalidOperationException($"Package {package.Key} does not exist in the store.");
        }

        if (package.State != PackageState.BuildRequested
            || !package.Manifest.IsPublishable)
        {
            throw new PackageConflictException(
                package.Key.ToString(),
                $"Package {package.Key} is not eligible for publication.");
        }

        _packages[package.Key] = package;
        return Task.CompletedTask;
    }

    public Task AcknowledgeDomainEventsAsync(
        PackageKey key,
        IReadOnlyCollection<IDomainEvent> publishedEvents,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(key);
        ArgumentNullException.ThrowIfNull(publishedEvents);

        return Task.CompletedTask;
    }

    public Task<IReadOnlyList<PackageIndexEntry>> SearchAsync(
        string? search,
        CancellationToken cancellationToken)
    {
        var normalizedSearch = search?.Trim();

        var entries = _packages.Values
            .Where(package => Matches(package, normalizedSearch))
            .OrderBy(package => package.Manifest.Name.Value, StringComparer.Ordinal)
            .ThenBy(package => package.Manifest.Version, PackageVersionOrder.Comparer)
            .Take(PackageIndexLimits.MaxListResults)
            .Select(package => new PackageIndexEntry(
                package.Manifest,
                package.State,
                package.RegisteredAt))
            .ToArray();

        return Task.FromResult<IReadOnlyList<PackageIndexEntry>>(entries);
    }

    private static bool Matches(Package package, string? search)
    {
        if (string.IsNullOrWhiteSpace(search))
        {
            return true;
        }

        return package.Manifest.Name.Value.Contains(search, StringComparison.OrdinalIgnoreCase)
            || package.Manifest.Source.CommitSha.StartsWith(search, StringComparison.OrdinalIgnoreCase);
    }
}
