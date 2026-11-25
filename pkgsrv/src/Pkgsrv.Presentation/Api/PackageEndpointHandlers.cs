using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Features.Packages.List;
using Pkgsrv.Application.Features.Packages.Register;
using Pkgsrv.Application.Features.Packages.RequestBuild;
using Pkgsrv.Presentation.Contracts;
using Pkgsrv.Presentation.Security;

namespace Pkgsrv.Presentation.Api;

public static class PackageEndpointHandlers
{
    public static async Task<IResult> ListAsync(
        IMediator mediator,
        IActorContextAccessor actors,
        CancellationToken cancellationToken)
    {
        try
        {
            var packages = await mediator.Send(
                new ListPackagesQuery(null, actors.GetCurrent()),
                cancellationToken);

            return Results.Ok(new PackageListResponse(
                packages.Select(PackageSummaryResponse.From).ToArray()));
        }
        catch (Exception exception) when (ApiErrorMapper.IsMapped(exception))
        {
            return ApiErrorMapper.Map(exception);
        }
    }

    public static async Task<IResult> RegisterAsync(
        RegisterPackageRequest? request,
        IMediator mediator,
        IActorContextAccessor actors,
        CancellationToken cancellationToken)
    {
        if (request is null)
        {
            return Results.Problem(
                statusCode: 400,
                title: "Validation failed",
                detail: "A package registration body is required.",
                extensions: new Dictionary<string, object?>
                {
                    ["code"] = "request.body.required"
                });
        }

        try
        {
            var result = await mediator.Send(
                new RegisterPackageCommand(
                    request.Name,
                    request.Version,
                    request.SourceUrl,
                    request.SourceCommit,
                    request.ToolchainName,
                    request.ToolchainVersion,
                    request.ContentHash,
                    request.Provenance,
                    request.Signature,
                    request.SignatureAlgorithm,
                    request.Isolation,
                    request.AllowNetwork,
                    request.TimeoutSeconds,
                    actors.GetCurrent()),
                cancellationToken);

            var body = PackageResponse.From(result.Package);

            return Results.Created($"/packages/{body.Name}/{body.Version}", body);
        }
        catch (Exception exception) when (ApiErrorMapper.IsMapped(exception))
        {
            return ApiErrorMapper.Map(exception);
        }
    }

    public static async Task<IResult> RequestBuildAsync(
        string name,
        string version,
        IMediator mediator,
        IActorContextAccessor actors,
        CancellationToken cancellationToken)
    {
        try
        {
            var result = await mediator.Send(
                new RequestPackageBuildCommand(name, version, actors.GetCurrent()),
                cancellationToken);

            return Results.Accepted(
                $"/packages/{result.Name}/{result.Version}",
                BuildResponse.From(result));
        }
        catch (Exception exception) when (ApiErrorMapper.IsMapped(exception))
        {
            return ApiErrorMapper.Map(exception);
        }
    }
}
