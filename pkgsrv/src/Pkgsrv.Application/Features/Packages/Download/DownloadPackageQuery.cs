using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Interfaces.Security;

namespace Pkgsrv.Application.Features.Packages.Download;

public sealed record DownloadPackageQuery(
    string? Name,
    string? Version,
    AuthorizationContext Actor) : IRequest<PackageDownloadResult>;

public sealed record PackageDownloadResult(
    string Name,
    string Version,
    string ContentHash,
    byte[] Content);
