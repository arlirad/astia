using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Artifacts;

public sealed class RefusingUnsignedPackagePublisher : IPackagePublisher
{
    public Task<PublishResult> PublishAsync(
        PackageManifest manifest,
        ArtifactMetadata artifact,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(manifest);
        ArgumentNullException.ThrowIfNull(artifact);

        if (artifact.Package != PackageKey.From(manifest.Name, manifest.Version))
        {
            return Task.FromResult(PublishResult.Refused(
                "Publication refused because artifact metadata belongs to a different package."));
        }

        if (!manifest.IsPublishable)
        {
            return Task.FromResult(PublishResult.Refused(
                "Publication refused because the package manifest is not signed."));
        }

        if (string.IsNullOrWhiteSpace(artifact.Signature))
        {
            return Task.FromResult(PublishResult.Refused(
                "Publication refused because the artifact has no signature."));
        }

        if (!string.Equals(artifact.ContentHash, manifest.ContentHash.Value, StringComparison.Ordinal))
        {
            return Task.FromResult(PublishResult.Refused(
                "Publication refused because artifact metadata does not match the package manifest."));
        }

        return Task.FromResult(PublishResult.Success());
    }
}
