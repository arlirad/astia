using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Abstractions.Caching;
using Pkgsrv.Application.Abstractions.Persistence;
using Pkgsrv.Application.Abstractions.Security;
using Pkgsrv.Application.Caching;
using Pkgsrv.Application.Contracts;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Validation;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Features.Packages.Get;

public sealed class GetPackageQueryHandler
    : IRequestHandler<GetPackageQuery, PackageDetailsDto>
{
    private readonly IPackageRepository _repository;
    private readonly IIrvaAuthorization _authorization;
    private readonly IPackageReadModelCache _cache;

    public GetPackageQueryHandler(
        IPackageRepository repository,
        IIrvaAuthorization authorization,
        IPackageReadModelCache? cache = null)
    {
        _repository = repository;
        _authorization = authorization;
        _cache = cache ?? NoOpPackageReadModelCache.Instance;
    }

    public async ValueTask<PackageDetailsDto> Handle(
        GetPackageQuery request,
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

        if (!authorization.IsAllowed)
        {
            throw new AuthorizationDeniedException(nameof(PackageAction.Read), authorization.Reason);
        }

        var cached = _cache.GetDetails(key);

        if (cached is not null)
        {
            return cached;
        }

        var generation = _cache.CaptureGeneration();
        var package = await _repository.FindAsync(key, cancellationToken).ConfigureAwait(false);

        if (package is null)
        {
            throw new PackageNotFoundException(key.ToString());
        }

        var details = PackageDetailsDto.From(package);
        _cache.SetDetails(details, generation);

        return details;
    }
}
