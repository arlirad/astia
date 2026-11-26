using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Artifacts;

public sealed class VerifyingPackagePublisher : IPackagePublisher
{
    private readonly IPackageSignatureVerifier _signatureVerifier;
    private readonly IArtifactStore _artifactStore;

    public VerifyingPackagePublisher(
        IPackageSignatureVerifier signatureVerifier,
        IArtifactStore artifactStore)
    {
        _signatureVerifier = signatureVerifier;
        _artifactStore = artifactStore;
    }

    public async Task<PublishResult> PublishAsync(
        PackageManifest manifest,
        ArtifactMetadata artifact,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(manifest);
        ArgumentNullException.ThrowIfNull(artifact);

        var expectedKey = PackageKey.From(manifest.Name, manifest.Version);

        if (artifact.Package != expectedKey)
        {
            return PublishResult.Refused(
                "Publication refused because artifact metadata belongs to a different package.");
        }

        if (!manifest.IsPublishable)
        {
            return PublishResult.Refused(
                "Publication refused because the package manifest is not signed.");
        }

        if (string.IsNullOrWhiteSpace(artifact.Signature)
            || !string.Equals(
                artifact.Signature,
                manifest.Signature.Value,
                StringComparison.Ordinal))
        {
            return PublishResult.Refused(
                "Publication refused because the artifact signature does not match the manifest signature.");
        }

        if (!string.Equals(
                artifact.ContentHash,
                manifest.ContentHash.Value,
                StringComparison.Ordinal))
        {
            return PublishResult.Refused(
                "Publication refused because artifact metadata does not match the package manifest.");
        }

        if (!_signatureVerifier.Verify(manifest.ContentHash, manifest.Signature))
        {
            return PublishResult.Refused(
                "Publication refused because the package signature could not be verified.");
        }

        var stored = await _artifactStore
            .FindAsync(expectedKey, manifest.ContentHash, cancellationToken)
            .ConfigureAwait(false);

        if (stored is null)
        {
            return PublishResult.Refused(
                "Publication refused because the content-addressed artifact was not found or failed hash verification.");
        }

        return PublishResult.Success();
    }
}
