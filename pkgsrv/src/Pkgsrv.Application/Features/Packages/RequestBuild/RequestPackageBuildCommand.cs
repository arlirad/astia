using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Abstractions.Security;
using Pkgsrv.Application.Contracts;

namespace Pkgsrv.Application.Features.Packages.RequestBuild;

public sealed record RequestPackageBuildCommand(
    string? Name,
    string? Version,
    AuthorizationContext Actor) : IRequest<RequestBuildResult>;
