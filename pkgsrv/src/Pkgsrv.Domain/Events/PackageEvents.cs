using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Domain.Events;

public sealed record PackageRegistered(
    PackageKey Key,
    PackageManifest Manifest,
    DateTimeOffset OccurredAt) : IDomainEvent;

public sealed record PackageBuildRequested(
    PackageKey Key,
    BuildPolicy BuildPolicy,
    DateTimeOffset OccurredAt) : IDomainEvent;
