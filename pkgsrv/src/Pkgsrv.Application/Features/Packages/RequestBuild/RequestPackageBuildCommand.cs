using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Contracts;

namespace Pkgsrv.Application.Features.Packages.RequestBuild;

public sealed record RequestPackageBuildCommand(
    string? Name,
    string? Version,
    AuthorizationContext Actor) : IRequest<RequestBuildResult>;
