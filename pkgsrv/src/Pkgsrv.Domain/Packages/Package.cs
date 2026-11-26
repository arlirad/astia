using Pkgsrv.Domain.Errors;
using Pkgsrv.Domain.Events;

namespace Pkgsrv.Domain.Packages;

public enum PackageState
{
    Registered = 0,
    BuildRequested = 1,
    Published = 2
}

public sealed class Package
{
    private readonly object _gate = new();
    private readonly List<IDomainEvent> _domainEvents = new();
    private PackageState _state = PackageState.Registered;
    private DateTimeOffset? _buildRequestedAt;
    private DateTimeOffset? _publishedAt;

    private Package(
        PackageKey key,
        PackageManifest manifest,
        BuildPolicy buildPolicy,
        DateTimeOffset registeredAt)
    {
        Key = key;
        Manifest = manifest;
        BuildPolicy = buildPolicy;
        RegisteredAt = registeredAt;
    }

    public PackageKey Key { get; }

    public PackageManifest Manifest { get; private set; }

    public BuildPolicy BuildPolicy { get; }

    public PackageState State
    {
        get
        {
            lock (_gate)
            {
                return _state;
            }
        }
    }

    public DateTimeOffset RegisteredAt { get; }

    public DateTimeOffset? BuildRequestedAt
    {
        get
        {
            lock (_gate)
            {
                return _buildRequestedAt;
            }
        }
    }

    public DateTimeOffset? PublishedAt
    {
        get
        {
            lock (_gate)
            {
                return _publishedAt;
            }
        }
    }

    public IReadOnlyList<IDomainEvent> DomainEvents
    {
        get
        {
            lock (_gate)
            {
                return _domainEvents.ToArray();
            }
        }
    }

    public static Package Register(
        PackageManifest manifest,
        BuildPolicy buildPolicy,
        DateTimeOffset registeredAt)
    {
        ArgumentNullException.ThrowIfNull(manifest);
        ArgumentNullException.ThrowIfNull(buildPolicy);

        var package = new Package(
            PackageKey.From(manifest.Name, manifest.Version),
            manifest,
            buildPolicy,
            registeredAt);

        package._domainEvents.Add(new PackageRegistered(package.Key, manifest, registeredAt));

        return package;
    }

    public static Package Rehydrate(
        PackageManifest manifest,
        BuildPolicy buildPolicy,
        PackageState state,
        DateTimeOffset registeredAt,
        DateTimeOffset? buildRequestedAt,
        IReadOnlyCollection<IDomainEvent>? pendingDomainEvents = null,
        DateTimeOffset? publishedAt = null)
    {
        ArgumentNullException.ThrowIfNull(manifest);
        ArgumentNullException.ThrowIfNull(buildPolicy);

        if (!Enum.IsDefined(state)
            || (state == PackageState.Registered
                && (buildRequestedAt is not null || publishedAt is not null))
            || (state == PackageState.BuildRequested
                && (buildRequestedAt is null || publishedAt is not null))
            || (state == PackageState.Published
                && (buildRequestedAt is null || publishedAt is null)))
        {
            throw new DomainException(
                DomainErrorCodes.PackageStateInvalid,
                "Persisted package state and timestamps are inconsistent.");
        }

        var package = new Package(
            PackageKey.From(manifest.Name, manifest.Version),
            manifest,
            buildPolicy,
            registeredAt);

        package._state = state;
        package._buildRequestedAt = buildRequestedAt;
        package._publishedAt = publishedAt;

        if (pendingDomainEvents is not null)
        {
            package._domainEvents.AddRange(pendingDomainEvents);
        }

        return package;
    }

    public void RequestBuild(DateTimeOffset requestedAt)
    {
        lock (_gate)
        {
            if (_state != PackageState.Registered)
            {
                throw new DomainException(
                    _state == PackageState.BuildRequested
                        ? DomainErrorCodes.BuildAlreadyRequested
                        : DomainErrorCodes.PackageStateInvalid,
                    "A build can only be requested for a registered package.");
            }

            _state = PackageState.BuildRequested;
            _buildRequestedAt = requestedAt;
            _domainEvents.Add(new PackageBuildRequested(Key, BuildPolicy, requestedAt));
        }
    }

    public void MarkPublished(DateTimeOffset publishedAt)
    {
        lock (_gate)
        {
            EnsureCanPublishUnsafe();
            _state = PackageState.Published;
            _publishedAt = publishedAt;
        }
    }

    public void EnsureCanPublish()
    {
        lock (_gate)
        {
            EnsureCanPublishUnsafe();
        }
    }

    private void EnsureCanPublishUnsafe()
    {
        if (_state != PackageState.BuildRequested)
        {
            throw new DomainException(
                DomainErrorCodes.PackageStateInvalid,
                "A package must have a requested build before it can be published.");
        }

        Manifest.Signature.EnsurePublishable();
    }

    public void ApplySignature(PackageSignature signature)
    {
        ArgumentNullException.ThrowIfNull(signature);
        signature.EnsurePublishable();

        lock (_gate)
        {
            Manifest = PackageManifest.Create(
                Manifest.Name,
                Manifest.Version,
                Manifest.Source,
                Manifest.Toolchain,
                Manifest.ContentHash,
                Manifest.Provenance,
                signature);
        }
    }

    public IReadOnlyList<IDomainEvent> PeekDomainEvents()
    {
        return DomainEvents;
    }

    public void AcknowledgeDomainEvents(IReadOnlyCollection<IDomainEvent> publishedEvents)
    {
        ArgumentNullException.ThrowIfNull(publishedEvents);

        lock (_gate)
        {
            foreach (var publishedEvent in publishedEvents)
            {
                for (var index = _domainEvents.Count - 1; index >= 0; index--)
                {
                    if (ReferenceEquals(_domainEvents[index], publishedEvent))
                    {
                        _domainEvents.RemoveAt(index);
                        break;
                    }
                }
            }
        }
    }
}
