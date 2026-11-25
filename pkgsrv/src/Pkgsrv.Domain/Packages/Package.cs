using Pkgsrv.Domain.Errors;
using Pkgsrv.Domain.Events;

namespace Pkgsrv.Domain.Packages;

public enum PackageState
{
    Registered = 0,
    BuildRequested = 1
}

public sealed class Package
{
    private readonly object _gate = new();
    private readonly List<IDomainEvent> _domainEvents = new();
    private PackageState _state = PackageState.Registered;
    private DateTimeOffset? _buildRequestedAt;

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

    public PackageManifest Manifest { get; }

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
        PackageKey key,
        PackageManifest manifest,
        BuildPolicy buildPolicy,
        DateTimeOffset registeredAt,
        PackageState state,
        DateTimeOffset? buildRequestedAt)
    {
        ArgumentNullException.ThrowIfNull(key);
        ArgumentNullException.ThrowIfNull(manifest);
        ArgumentNullException.ThrowIfNull(buildPolicy);

        if (!Enum.IsDefined(state)
            || (state == PackageState.Registered && buildRequestedAt is not null)
            || (state == PackageState.BuildRequested && buildRequestedAt is null))
        {
            throw new DomainException(
                DomainErrorCodes.PackageStateInvalid,
                "Persisted package state is inconsistent.");
        }

        var package = new Package(key, manifest, buildPolicy, registeredAt)
        {
            _state = state,
            _buildRequestedAt = buildRequestedAt
        };

        return package;
    }

    public void RequestBuild(DateTimeOffset requestedAt)
    {
        lock (_gate)
        {
            if (_state == PackageState.BuildRequested)
            {
                throw new DomainException(
                    DomainErrorCodes.BuildAlreadyRequested,
                    "A build has already been requested for this package version.");
            }

            _state = PackageState.BuildRequested;
            _buildRequestedAt = requestedAt;
            _domainEvents.Add(new PackageBuildRequested(Key, BuildPolicy, requestedAt));
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
