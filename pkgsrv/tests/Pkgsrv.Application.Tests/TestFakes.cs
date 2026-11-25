using Pkgsrv.Application.Abstractions.Builds;
using Pkgsrv.Application.Abstractions.Events;
using Pkgsrv.Application.Abstractions.Persistence;
using Pkgsrv.Application.Abstractions.Security;
using Pkgsrv.Application.Abstractions.Sources;
using Pkgsrv.Application.Abstractions.Time;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Application.Tests;

internal sealed class TestRepository : IPackageRepository, IPackageIndex
{
    private readonly Dictionary<PackageKey, Package> _packages = new();

    public async Task<Package?> FindAsync(PackageKey key, CancellationToken cancellationToken)
    {
        await Task.CompletedTask;
        return _packages.GetValueOrDefault(key);
    }

    public async Task<bool> TryAddAsync(Package package, CancellationToken cancellationToken)
    {
        await Task.CompletedTask;
        return _packages.TryAdd(package.Key, package);
    }

    public async Task UpdateAsync(Package package, CancellationToken cancellationToken)
    {
        await Task.CompletedTask;

        if (!_packages.ContainsKey(package.Key))
        {
            throw new InvalidOperationException($"Package {package.Key} does not exist.");
        }

        _packages[package.Key] = package;
    }

    public async Task<IReadOnlyList<PackageIndexEntry>> SearchAsync(
        string? search,
        CancellationToken cancellationToken)
    {
        await Task.CompletedTask;

        return _packages.Values
            .Where(package => string.IsNullOrWhiteSpace(search)
                || package.Manifest.Name.Value.Contains(search, StringComparison.OrdinalIgnoreCase))
            .Select(package => new PackageIndexEntry(
                package.Manifest,
                package.State,
                package.RegisteredAt))
            .ToArray();
    }
}

internal sealed class TestAuthorization : IIrvaAuthorization
{
    public bool Allow { get; set; } = true;

    public List<PackageAction> Actions { get; } = new();

    public List<PackageKey?> Packages { get; } = new();

    public Task<AuthorizationResult> AuthorizeAsync(
        AuthorizationContext context,
        PackageAction action,
        PackageKey? package,
        CancellationToken cancellationToken)
    {
        Actions.Add(action);
        Packages.Add(package);

        return Task.FromResult(Allow
            ? AuthorizationResult.Allow()
            : AuthorizationResult.Deny("test denial"));
    }
}

internal sealed class TestSourceAllowlist : ISourceAllowlist
{
    public bool Allowed { get; set; } = true;

    public PackageSource? LastSource { get; private set; }

    public bool IsAllowed(PackageSource source)
    {
        LastSource = source;
        return Allowed;
    }
}

internal sealed class TestEventPublisher : IDomainEventPublisher
{
    public List<IDomainEvent> Events { get; } = new();

    public Task PublishAsync(
        IReadOnlyCollection<IDomainEvent> domainEvents,
        CancellationToken cancellationToken)
    {
        Events.AddRange(domainEvents);
        return Task.CompletedTask;
    }
}

internal sealed class ThrowingEventPublisher : IDomainEventPublisher
{
    public List<IDomainEvent> Attempted { get; } = new();

    public Task PublishAsync(
        IReadOnlyCollection<IDomainEvent> domainEvents,
        CancellationToken cancellationToken)
    {
        Attempted.AddRange(domainEvents);
        throw new InvalidOperationException("event publication failed");
    }
}

internal sealed class TestClock : ISystemClock
{
    public DateTimeOffset UtcNow { get; set; } = TestPackages.Timestamp;
}

internal sealed class TestBuildSandbox : IBuildSandbox
{
    public BuildSandboxResult Result { get; set; } =
        BuildSandboxResult.NotExecuted("test safe sandbox did not execute.");

    public BuildSandboxRequest? LastRequest { get; private set; }

    public Task<BuildSandboxResult> ExecuteAsync(
        BuildSandboxRequest request,
        CancellationToken cancellationToken)
    {
        LastRequest = request;
        return Task.FromResult(Result);
    }
}
