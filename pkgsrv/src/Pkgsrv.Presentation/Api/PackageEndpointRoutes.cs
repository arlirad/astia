using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Presentation.Caching;
using Pkgsrv.Presentation.Contracts;
using Pkgsrv.Presentation.Security;

namespace Pkgsrv.Presentation.Api;

public static class PackageEndpointRoutes
{
    public static IEndpointRouteBuilder MapPackageEndpoints(this IEndpointRouteBuilder endpoints)
    {
        var packages = endpoints.MapGroup("/packages");

        packages.MapGet(
            string.Empty,
            (IMediator mediator, IActorContextAccessor actors, IPackageJsonCache cache, CancellationToken cancellationToken) =>
                PackageEndpointHandlers.ListAsync(mediator, actors, cache, cancellationToken));

        packages.MapGet(
            "/{name}/{version}",
            (string name, string version, IMediator mediator, IActorContextAccessor actors, IPackageJsonCache cache, CancellationToken cancellationToken) =>
                PackageEndpointHandlers.GetAsync(name, version, mediator, actors, cache, cancellationToken));

        packages.MapPost(
            string.Empty,
            (RegisterPackageRequest request, IMediator mediator, IActorContextAccessor actors, IPackageJsonCache cache, CancellationToken cancellationToken) =>
                PackageEndpointHandlers.RegisterAsync(request, mediator, actors, cache, cancellationToken));

        packages.MapPost(
            "/{name}/{version}/build",
            (string name, string version, IMediator mediator, IActorContextAccessor actors, IPackageJsonCache cache, CancellationToken cancellationToken) =>
                PackageEndpointHandlers.RequestBuildAsync(name, version, mediator, actors, cache, cancellationToken));

        return endpoints;
    }
}
