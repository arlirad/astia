using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Application.Interfaces.Security;
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

    public GetPackageQueryHandler(
        IPackageRepository repository,
        IIrvaAuthorization authorization)
    {
        _repository = repository;
        _authorization = authorization;
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

        var package = await _repository.FindAsync(key, cancellationToken).ConfigureAwait(false);

        if (package is null)
        {
            throw new PackageNotFoundException(key.ToString());
        }

        return PackageDetailsDto.From(package);
    }
}
