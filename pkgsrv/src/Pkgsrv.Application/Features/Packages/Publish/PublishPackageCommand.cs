using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Interfaces.Security;

namespace Pkgsrv.Application.Features.Packages.Publish;

public sealed record PublishPackageCommand(
    string? Name,
    string? Version,
    AuthorizationContext Actor) : IRequest<PublishPackageResult>;

public sealed record PublishPackageResult(
    string Name,
    string Version,
    string ContentHash,
    string Signature);
