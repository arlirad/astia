using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Validation;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Features.Packages.Download;

public sealed class DownloadPackageQueryHandler
    : IRequestHandler<DownloadPackageQuery, PackageDownloadResult>
{
    private readonly IPackageRepository _repository;
    private readonly IArtifactStore _artifactStore;
    private readonly IPackageSignatureVerifier _signatureVerifier;
    private readonly IIrvaAuthorization _authorization;

    public DownloadPackageQueryHandler(
        IPackageRepository repository,
        IArtifactStore artifactStore,
        IPackageSignatureVerifier signatureVerifier,
        IIrvaAuthorization authorization)
    {
        _repository = repository;
        _artifactStore = artifactStore;
        _signatureVerifier = signatureVerifier;
        _authorization = authorization;
    }

    public async ValueTask<PackageDownloadResult> Handle(
        DownloadPackageQuery request,
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
            PackageAction.Read,
            key,
            cancellationToken).ConfigureAwait(false);
        authorization.EnsureAuthorized(nameof(PackageAction.Read));

        var package = await _repository.FindAsync(key, cancellationToken).ConfigureAwait(false);

        if (package is null)
        {
            throw new PackageNotFoundException(key.ToString());
        }

        if (package.State != PackageState.Published || package.PublishedAt is null)
        {
            throw new PackageNotPublishedException(key.ToString());
        }

        if (!package.Manifest.IsPublishable
            || !_signatureVerifier.Verify(package.Manifest.ContentHash, package.Manifest.Signature))
        {
            throw new PackageSignatureInvalidException(key.ToString());
        }

        var artifact = await _artifactStore
            .FindAsync(key, package.Manifest.ContentHash, cancellationToken)
            .ConfigureAwait(false);

        if (artifact is null)
        {
            throw new PackageArtifactNotFoundException(key.ToString(), package.Manifest.ContentHash.Value);
        }

        return new PackageDownloadResult(
            key.Name.Value,
            key.Version.ToString(),
            artifact.ContentHash.Value,
            artifact.Bytes.ToArray());
    }
}
