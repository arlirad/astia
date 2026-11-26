using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Application.Interfaces.Caching;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Interfaces.Time;
using Pkgsrv.Application.Validation;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Features.Packages.Publish;

public sealed class PublishPackageCommandHandler
    : IRequestHandler<PublishPackageCommand, PublishPackageResult>
{
    private readonly IPackageRepository _repository;
    private readonly IArtifactStore _artifactStore;
    private readonly IArtifactMetadataStore _artifactMetadataStore;
    private readonly IPackageSigner _signer;
    private readonly IPackageSignatureVerifier _signatureVerifier;
    private readonly IPackagePublisher _publisher;
    private readonly IIrvaAuthorization _authorization;
    private readonly ISystemClock _clock;
    private readonly IPackageCacheInvalidator? _cacheInvalidator;

    public PublishPackageCommandHandler(
        IPackageRepository repository,
        IArtifactStore artifactStore,
        IArtifactMetadataStore artifactMetadataStore,
        IPackageSigner signer,
        IPackageSignatureVerifier signatureVerifier,
        IPackagePublisher publisher,
        IIrvaAuthorization authorization,
        ISystemClock clock,
        IPackageCacheInvalidator? cacheInvalidator = null)
    {
        _repository = repository;
        _artifactStore = artifactStore;
        _artifactMetadataStore = artifactMetadataStore;
        _signer = signer;
        _signatureVerifier = signatureVerifier;
        _publisher = publisher;
        _authorization = authorization;
        _clock = clock;
        _cacheInvalidator = cacheInvalidator;
    }

    public async ValueTask<PublishPackageResult> Handle(
        PublishPackageCommand request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);

        var failures = PackageKeyValidator.Validate(request.Name, request.Version);

        if (failures.Count > 0)
        {
            throw new ValidationFailedException(failures);
        }

        var key = PackageKey.Create(request.Name, request.Version);
        var actor = request.Actor ?? AuthorizationContext.Anonymous;
        var authorization = await _authorization.AuthorizeAsync(
            actor,
            PackageAction.Publish,
            key,
            cancellationToken).ConfigureAwait(false);
        authorization.EnsureAuthorized(nameof(PackageAction.Publish));

        var package = await _repository.FindAsync(key, cancellationToken).ConfigureAwait(false);

        if (package is null)
        {
            throw new PackageNotFoundException(key.ToString());
        }

        if (package.State == PackageState.Published)
        {
            return await ReturnPersistedPublicationAsync(package, key, cancellationToken)
                .ConfigureAwait(false);
        }

        if (package.State != PackageState.BuildRequested)
        {
            throw new PackagePublishException(
                key.ToString(),
                "a build must be requested before publication.");
        }

        var artifact = await _artifactStore
            .FindAsync(key, package.Manifest.ContentHash, cancellationToken)
            .ConfigureAwait(false);

        if (artifact is null)
        {
            throw new PackageArtifactNotFoundException(
                key.ToString(),
                package.Manifest.ContentHash.Value);
        }

        if (!package.Manifest.IsPublishable)
        {
            var signing = await _signer
                .SignAsync(package.Manifest.ContentHash, cancellationToken)
                .ConfigureAwait(false);

            if (!signing.Signed)
            {
                throw new PackagePublishException(
                    key.ToString(),
                    signing.Detail);
            }

            if (!_signatureVerifier.Verify(package.Manifest.ContentHash, signing.Signature))
            {
                throw new PackageSignatureInvalidException(key.ToString());
            }

            package.ApplySignature(signing.Signature);
            await _repository
                .UpdateSignatureAsync(package, cancellationToken)
                .ConfigureAwait(false);
        }
        else if (!_signatureVerifier.Verify(package.Manifest.ContentHash, package.Manifest.Signature))
        {
            throw new PackageSignatureInvalidException(key.ToString());
        }

        var metadata = new ArtifactMetadata(
            key,
            package.Manifest.ContentHash.Value,
            package.Manifest.Signature.Value,
            _clock.UtcNow);
        await _artifactMetadataStore.SaveAsync(metadata, cancellationToken).ConfigureAwait(false);

        var publication = await _publisher
            .PublishAsync(package.Manifest, metadata, cancellationToken)
            .ConfigureAwait(false);

        if (!publication.Published)
        {
            throw new PackagePublishException(
                key.ToString(),
                publication.Detail);
        }

        package.EnsureCanPublish();
        var publishedAt = _clock.UtcNow;
        await _repository
            .MarkPublishedAsync(package, publishedAt, cancellationToken)
            .ConfigureAwait(false);
        package.MarkPublished(publishedAt);

        _cacheInvalidator?.InvalidateAfterMutation();

        return new PublishPackageResult(
            key.Name.Value,
            key.Version.ToString(),
            package.Manifest.ContentHash.Value,
            package.Manifest.Signature.Value!);
    }

    private async ValueTask<PublishPackageResult> ReturnPersistedPublicationAsync(
        Package package,
        PackageKey key,
        CancellationToken cancellationToken)
    {
        if (package.PublishedAt is null)
        {
            throw new PackagePublishException(
                key.ToString(),
                "durable publication state has no publication timestamp.");
        }

        if (!package.Manifest.IsPublishable
            || !_signatureVerifier.Verify(
                package.Manifest.ContentHash,
                package.Manifest.Signature))
        {
            throw new PackageSignatureInvalidException(key.ToString());
        }

        var artifact = await _artifactStore
            .FindAsync(key, package.Manifest.ContentHash, cancellationToken)
            .ConfigureAwait(false);

        if (artifact is null
            || artifact.Package != key
            || artifact.ContentHash != package.Manifest.ContentHash)
        {
            throw new PackageArtifactNotFoundException(
                key.ToString(),
                package.Manifest.ContentHash.Value);
        }

        return new PublishPackageResult(
            key.Name.Value,
            key.Version.ToString(),
            package.Manifest.ContentHash.Value,
            package.Manifest.Signature.Value!);
    }
}
