using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Abstractions.Security;
using Pkgsrv.Application.Contracts;

namespace Pkgsrv.Application.Features.Packages.List;

public sealed record ListPackagesQuery(
    string? Search,
    AuthorizationContext Actor) : IRequest<IReadOnlyList<PackageSummaryDto>>;
