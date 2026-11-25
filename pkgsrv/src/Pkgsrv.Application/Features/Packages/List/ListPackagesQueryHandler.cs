using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Abstractions.Caching;
using Pkgsrv.Application.Abstractions.Persistence;
using Pkgsrv.Application.Abstractions.Security;
using Pkgsrv.Application.Caching;
using Pkgsrv.Application.Contracts;
using Pkgsrv.Application.Errors;

namespace Pkgsrv.Application.Features.Packages.List;

public sealed class ListPackagesQueryHandler
    : IRequestHandler<ListPackagesQuery, IReadOnlyList<PackageSummaryDto>>
{
    private readonly IPackageIndex _index;
    private readonly IIrvaAuthorization _authorization;
    private readonly IPackageReadModelCache _cache;

    public ListPackagesQueryHandler(
        IPackageIndex index,
        IIrvaAuthorization authorization,
        IPackageReadModelCache? cache = null)
    {
        _index = index;
        _authorization = authorization;
        _cache = cache ?? NoOpPackageReadModelCache.Instance;
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

        var cached = _cache.GetList(request.Search);

        if (cached is not null)
        {
            return cached;
        }

        var generation = _cache.CaptureGeneration();
        var entries = await _index.SearchAsync(request.Search, cancellationToken).ConfigureAwait(false);
        var packages = entries.Select(PackageSummaryDto.From).ToArray();
        _cache.SetList(request.Search, packages, generation);

        return packages;
    }
}
