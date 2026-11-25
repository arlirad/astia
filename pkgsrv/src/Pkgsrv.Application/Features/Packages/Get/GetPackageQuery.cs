using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Contracts;

namespace Pkgsrv.Application.Features.Packages.Get;

public sealed record GetPackageQuery(
    string? Name,
    string? Version,
    AuthorizationContext Actor) : IRequest<PackageDetailsDto>;
