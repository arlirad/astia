using System.Collections.Concurrent;
using System.Security.Cryptography;
using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Artifacts;

public sealed class InMemoryArtifactStore : IArtifactStore
{
    private readonly ConcurrentDictionary<(PackageKey Package, string Hash), byte[]> _artifacts = new();

    public Task<StoredArtifact> StoreAsync(
        PackageKey package,
        ReadOnlyMemory<byte> bytes,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(package);

        var hash = Convert.ToHexString(SHA256.HashData(bytes.Span)).ToLowerInvariant();
        _artifacts[(package, hash)] = bytes.ToArray();
        return Task.FromResult(new StoredArtifact(package, ContentHash.Create(hash), bytes.Length));
    }

    public Task<ArtifactContent?> FindAsync(
        PackageKey package,
        ContentHash contentHash,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(package);
        ArgumentNullException.ThrowIfNull(contentHash);

        if (!_artifacts.TryGetValue((package, contentHash.Value), out var bytes))
        {
            return Task.FromResult<ArtifactContent?>(null);
        }

        var actual = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

        return Task.FromResult<ArtifactContent?>(actual == contentHash.Value
            ? new ArtifactContent(package, contentHash, bytes)
            : null);
    }
}
