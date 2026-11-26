using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Interfaces.Artifacts;

public sealed record ArtifactMetadata(
    PackageKey Package,
    string ContentHash,
    string? Signature,
    DateTimeOffset RecordedAt);

public interface IArtifactMetadataStore
{
    Task SaveAsync(ArtifactMetadata metadata, CancellationToken cancellationToken);

    Task<ArtifactMetadata?> FindAsync(PackageKey key, CancellationToken cancellationToken);
}

public sealed record SigningResult(PackageSignature Signature, string Detail)
{
    public bool Signed => Signature.IsPublishable;
}

public interface IPackageSigner
{
    Task<SigningResult> SignAsync(ContentHash contentHash, CancellationToken cancellationToken);
}

public sealed record PublishResult(bool Published, string Detail)
{
    public static PublishResult Success(string detail = "published") => new(true, detail);

    public static PublishResult Refused(string detail) => new(false, detail);
}

public interface IPackagePublisher
{
    Task<PublishResult> PublishAsync(
        PackageManifest manifest,
        ArtifactMetadata artifact,
        CancellationToken cancellationToken);
}

public sealed record StoredArtifact(
    PackageKey Package,
    ContentHash ContentHash,
    long Length);

public sealed record ArtifactContent(
    PackageKey Package,
    ContentHash ContentHash,
    ReadOnlyMemory<byte> Bytes);

public interface IArtifactStore
{
    Task<StoredArtifact> StoreAsync(
        PackageKey package,
        ReadOnlyMemory<byte> bytes,
        CancellationToken cancellationToken);

    Task<ArtifactContent?> FindAsync(
        PackageKey package,
        ContentHash contentHash,
        CancellationToken cancellationToken);
}

public interface IPackageSignatureVerifier
{
    bool Verify(ContentHash contentHash, PackageSignature signature);
}

public static class PackageSignatureAlgorithms
{
    public const string RsaSha256 = "rsa-sha256";
}

public sealed class RefusingPackageSignatureVerifier : IPackageSignatureVerifier
{
    public bool Verify(ContentHash contentHash, PackageSignature signature) => false;
}
