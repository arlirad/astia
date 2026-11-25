using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Abstractions.Persistence;
using Pkgsrv.Application.Abstractions.Security;
using Pkgsrv.Application.Contracts;
using Pkgsrv.Application.Errors;

namespace Pkgsrv.Application.Features.Packages.List;

public sealed class ListPackagesQueryHandler
    : IRequestHandler<ListPackagesQuery, IReadOnlyList<PackageSummaryDto>>
{
    private readonly IPackageIndex _index;
    private readonly IIrvaAuthorization _authorization;

    public ListPackagesQueryHandler(IPackageIndex index, IIrvaAuthorization authorization)
    {
        _index = index;
        _authorization = authorization;
    }

    public async ValueTask<IReadOnlyList<PackageSummaryDto>> Handle(
        ListPackagesQuery request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);

        var actor = request.Actor ?? AuthorizationContext.Anonymous;
        var authorization = await _authorization.AuthorizeAsync(
            actor,
            PackageAction.Read,
            null,
            cancellationToken).ConfigureAwait(false);

        if (!authorization.IsAllowed)
        {
            throw new AuthorizationDeniedException(nameof(PackageAction.Read), authorization.Reason);
        }

        var entries = await _index.SearchAsync(request.Search, cancellationToken).ConfigureAwait(false);

        return entries.Select(PackageSummaryDto.From).ToArray();
    }
}
