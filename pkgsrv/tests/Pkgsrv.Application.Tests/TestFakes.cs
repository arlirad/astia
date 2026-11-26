using System.Text;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Application.Interfaces.Builds;
using Pkgsrv.Application.Interfaces.Caching;
using Pkgsrv.Application.Interfaces.Events;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Interfaces.Sources;
using Pkgsrv.Application.Interfaces.Time;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;
using Pkgsrv.TestSupport;
using System.Security.Cryptography;

namespace Pkgsrv.Application.Tests;

internal sealed class TestRepository : IPackageRepository, IPackageIndex
{
    private readonly Dictionary<PackageKey, Package> _packages = new();

    public List<(PackageKey Key, IReadOnlyCollection<IDomainEvent> Events)> Acknowledgements { get; } = new();

    public bool ThrowOnAcknowledge { get; set; }

    public bool ThrowOnUpdate { get; set; }

    public bool ThrowOnMarkPublished { get; set; }

    public int MarkPublishedCalls { get; private set; }

    public TaskCompletionSource MarkPublishedStarted { get; } = new(
        TaskCreationOptions.RunContinuationsAsynchronously);

    public Func<Task>? MarkPublishedBarrier { get; set; }

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
        ArgumentNullException.ThrowIfNull(package);

        if (ThrowOnUpdate)
        {
            throw new PackageConflictException(
                package.Key.ToString(),
                $"Package {package.Key} was modified by another request.");
        }

        if (!_packages.ContainsKey(package.Key))
        {
            throw new InvalidOperationException($"Package {package.Key} does not exist.");
        }

        _packages[package.Key] = package;
    }

    public async Task UpdateSignatureAsync(Package package, CancellationToken cancellationToken)
    {
        await Task.CompletedTask;

        if (!_packages.ContainsKey(package.Key))
        {
            throw new InvalidOperationException($"Package {package.Key} does not exist.");
        }

        _packages[package.Key] = package;
    }

    public async Task MarkPublishedAsync(
        Package package,
        DateTimeOffset publishedAt,
        CancellationToken cancellationToken)
    {
        await Task.CompletedTask;

        MarkPublishedCalls++;
        MarkPublishedStarted.TrySetResult();

        if (MarkPublishedBarrier is not null)
        {
            await MarkPublishedBarrier();
        }

        if (ThrowOnMarkPublished)
        {
            throw new PackageConflictException(
                package.Key.ToString(),
                $"Package {package.Key} was modified by another request.");
        }

        if (!_packages.ContainsKey(package.Key))
        {
            throw new InvalidOperationException($"Package {package.Key} does not exist.");
        }

        _packages[package.Key] = package;
    }

    public async Task AcknowledgeDomainEventsAsync(
        PackageKey key,
        IReadOnlyCollection<IDomainEvent> publishedEvents,
        CancellationToken cancellationToken)
    {
        await Task.CompletedTask;

        if (ThrowOnAcknowledge)
        {
            throw new InvalidOperationException("event acknowledgement persistence failed");
        }

        Acknowledgements.Add((key, publishedEvents));
    }

    public async Task<IReadOnlyList<PackageIndexEntry>> SearchAsync(
        string? search,
        CancellationToken cancellationToken)
    {
        await Task.CompletedTask;

        return _packages.Values
            .Where(package => string.IsNullOrWhiteSpace(search)
                || package.Manifest.Name.Value.Contains(search, StringComparison.OrdinalIgnoreCase)
                || package.Manifest.Source.CommitSha.StartsWith(search, StringComparison.OrdinalIgnoreCase))
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

internal sealed class ThrowingBuildSandbox : IBuildSandbox
{
    public Task<BuildSandboxResult> ExecuteAsync(
        BuildSandboxRequest request,
        CancellationToken cancellationToken) =>
        throw new InvalidOperationException("sandbox failed");
}

internal sealed class TestArtifactStore : IArtifactStore
{
    private readonly Dictionary<(PackageKey Package, string Hash), byte[]> _artifacts = new();

    public Task<StoredArtifact> StoreAsync(
        PackageKey package,
        ReadOnlyMemory<byte> bytes,
        CancellationToken cancellationToken)
    {
        var hash = Convert.ToHexString(SHA256.HashData(bytes.Span)).ToLowerInvariant();
        _artifacts[(package, hash)] = bytes.ToArray();
        return Task.FromResult(new StoredArtifact(package, ContentHash.Create(hash), bytes.Length));
    }

    public Task<ArtifactContent?> FindAsync(
        PackageKey package,
        ContentHash contentHash,
        CancellationToken cancellationToken)
    {
        if (!_artifacts.TryGetValue((package, contentHash.Value), out var bytes))
        {
            return Task.FromResult<ArtifactContent?>(null);
        }

        var actual = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

        return Task.FromResult<ArtifactContent?>(actual == contentHash.Value
            ? new ArtifactContent(package, contentHash, bytes)
            : null);
    }

    public void Tamper(PackageKey package, ContentHash contentHash)
    {
        _artifacts[(package, contentHash.Value)] = "tampered"u8.ToArray();
    }

    public void Remove(PackageKey package, ContentHash contentHash)
    {
        _artifacts.Remove((package, contentHash.Value));
    }
}

internal sealed class TestSigner : IPackageSigner
{
    public SigningResult Result { get; set; } = new(
        PackageSignature.Signed("rsa-sha256", "test-signature"),
        "test signer");

    public int Calls { get; private set; }

    public Task<SigningResult> SignAsync(ContentHash contentHash, CancellationToken cancellationToken)
    {
        Calls++;
        return Task.FromResult(Result);
    }
}

internal sealed class TestSignatureVerifier : IPackageSignatureVerifier
{
    public bool Allowed { get; set; } = true;

    public bool Verify(ContentHash contentHash, PackageSignature signature) => Allowed;
}

internal sealed class TestArtifactMetadataStore : IArtifactMetadataStore
{
    private readonly Dictionary<PackageKey, ArtifactMetadata> _metadata = new();

    public ArtifactMetadata? LastSaved { get; private set; }

    public Task SaveAsync(ArtifactMetadata metadata, CancellationToken cancellationToken)
    {
        LastSaved = metadata;
        _metadata[metadata.Package] = metadata;
        return Task.CompletedTask;
    }

    public Task<ArtifactMetadata?> FindAsync(PackageKey key, CancellationToken cancellationToken) =>
        Task.FromResult(_metadata.GetValueOrDefault(key));

    public void Remove(PackageKey key)
    {
        _metadata.Remove(key);
    }

    public void Tamper(PackageKey key)
    {
        if (_metadata.TryGetValue(key, out var metadata))
        {
            _metadata[key] = metadata with { ContentHash = new string('0', 64) };
        }
    }
}

internal sealed class TestPublisher : IPackagePublisher
{
    public PublishResult Result { get; set; } = PublishResult.Success();

    public Action? OnPublish { get; set; }

    public PackageManifest? LastManifest { get; private set; }

    public int Calls { get; private set; }

    public Task<PublishResult> PublishAsync(
        PackageManifest manifest,
        ArtifactMetadata artifact,
        CancellationToken cancellationToken)
    {
        Calls++;
        LastManifest = manifest;
        OnPublish?.Invoke();
        return Task.FromResult(Result);
    }
}

internal sealed class ThrowingArtifactStore : IArtifactStore
{
    public Task<StoredArtifact> StoreAsync(
        PackageKey package,
        ReadOnlyMemory<byte> bytes,
        CancellationToken cancellationToken) =>
        throw new InvalidOperationException("artifact store failed");

    public Task<ArtifactContent?> FindAsync(
        PackageKey package,
        ContentHash contentHash,
        CancellationToken cancellationToken) =>
        throw new InvalidOperationException("artifact store failed");
}

internal sealed class RecordingPackageCacheInvalidationNotifier
    : IPackageCacheInvalidationNotifier
{
    public List<PackageCacheInvalidation> Invalidations { get; } = new();

    public int Calls => Invalidations.Count;

    public Task NotifyAsync(
        PackageCacheInvalidation invalidation,
        CancellationToken cancellationToken)
    {
        Invalidations.Add(invalidation);
        return Task.CompletedTask;
    }
}
