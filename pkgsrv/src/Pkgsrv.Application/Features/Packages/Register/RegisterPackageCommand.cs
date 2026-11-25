using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Contracts;

namespace Pkgsrv.Application.Features.Packages.Register;

public sealed record RegisterPackageCommand(
    string? Name,
    string? Version,
    string? SourceUrl,
    string? SourceCommit,
    string? ToolchainName,
    string? ToolchainVersion,
    string? ContentHash,
    string? Provenance,
    string? Signature,
    string? SignatureAlgorithm,
    string? Isolation,
    bool AllowNetwork,
    int TimeoutSeconds,
    AuthorizationContext Actor) : IRequest<RegisterPackageResult>;
