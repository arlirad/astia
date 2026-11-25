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
