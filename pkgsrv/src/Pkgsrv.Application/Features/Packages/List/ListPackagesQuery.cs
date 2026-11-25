using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Contracts;

namespace Pkgsrv.Application.Features.Packages.List;

public sealed record ListPackagesQuery(
    string? Search,
    AuthorizationContext Actor) : IRequest<IReadOnlyList<PackageSummaryDto>>;
