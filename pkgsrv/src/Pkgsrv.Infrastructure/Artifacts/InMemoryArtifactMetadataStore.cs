using System.Collections.Concurrent;
using Pkgsrv.Application.Abstractions.Artifacts;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Artifacts;

public sealed class InMemoryArtifactMetadataStore : IArtifactMetadataStore
{
    private readonly ConcurrentDictionary<PackageKey, ArtifactMetadata> _artifacts = new();

    public Task SaveAsync(ArtifactMetadata metadata, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(metadata);
        _artifacts[metadata.Package] = metadata;
        return Task.CompletedTask;
    }

    public Task<ArtifactMetadata?> FindAsync(PackageKey key, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(key);
        _artifacts.TryGetValue(key, out var metadata);
        return Task.FromResult(metadata);
    }
}
