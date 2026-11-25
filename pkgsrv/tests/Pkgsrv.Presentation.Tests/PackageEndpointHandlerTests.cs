using System.Reflection;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Http.HttpResults;
using Pkgsrv.Application.Abstractions.Persistence;
using Pkgsrv.Application.Abstractions.Security;
using Pkgsrv.Application.Contracts;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Features.Packages.List;
using Pkgsrv.Application.Features.Packages.Register;
using Pkgsrv.Application.Validation;
using Pkgsrv.Domain.Errors;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Presentation.Api;
using Pkgsrv.Presentation.Contracts;
using Pkgsrv.Presentation.Security;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Presentation.Tests;

public sealed class PackageEndpointHandlerTests
{
    [Fact]
    public async Task List_MapsSuccessfulQueryAndPropagatesActorHeaders()
    {
        var mediator = new StubMediator
        {
            Handler = (_, _) => ValueTask.FromResult<object>(
                new[] { PackageSummaryDto.From(SummaryEntry()) })
        };
        var accessor = CreateAccessor("alice", "reader, builder", AccountId);

        var result = await PackageEndpointHandlers.ListAsync(mediator, accessor, CancellationToken.None);

        var ok = Assert.IsType<Ok<PackageListResponse>>(result);
        Assert.Equal("widget", Assert.Single(ok.Value!.Packages).Name);

        var query = Assert.IsType<ListPackagesQuery>(mediator.LastRequest);
        Assert.Equal("alice", query.Actor.ActorId);
        Assert.Equal(AccountId, query.Actor.AccountId);
        Assert.Contains("builder", query.Actor.Roles);
    }

    [Fact]
    public async Task List_DefaultsToAnonymousWithoutActorHeader()
    {
        var mediator = new StubMediator
        {
            Handler = (_, _) => ValueTask.FromResult<object>(Array.Empty<PackageSummaryDto>())
        };
        var accessor = new HeaderActorContextAccessor(new HttpContextAccessor
        {
            HttpContext = new DefaultHttpContext()
        });

        await PackageEndpointHandlers.ListAsync(mediator, accessor, CancellationToken.None);

        var query = Assert.IsType<ListPackagesQuery>(mediator.LastRequest);
        Assert.True(query.Actor.IsAnonymous);
    }

    [Fact]
    public async Task Register_ReturnsCreatedWithPackageBody()
    {
        var mediator = new StubMediator
        {
            Handler = (_, _) => ValueTask.FromResult<object>(
                new RegisterPackageResult(PackageDetailsDto.From(TestPackages.CreatePackage())))
        };

        var result = await PackageEndpointHandlers.RegisterAsync(
            CreateRegistrationRequest(),
            mediator,
            CreateAccessor("publisher"),
            CancellationToken.None);

        var created = Assert.IsType<Created<PackageResponse>>(result);
        Assert.Equal("/packages/widget/1.2.3", created.Location);
        Assert.Equal("widget", created.Value!.Name);

        var command = Assert.IsType<RegisterPackageCommand>(mediator.LastRequest);
        Assert.Equal("publisher", command.Actor.ActorId);
        Assert.Equal("Container", command.Isolation);
    }

    [Fact]
    public async Task Register_RejectsMissingBodyWithoutDispatching()
    {
        var mediator = new StubMediator();

        var result = await PackageEndpointHandlers.RegisterAsync(
            null,
            mediator,
            CreateAccessor("publisher"),
            CancellationToken.None);

        var problem = Assert.IsType<ProblemHttpResult>(result);
        Assert.Equal(400, problem.ProblemDetails.Status);
        Assert.Equal("request.body.required", problem.ProblemDetails.Extensions["code"]);
        Assert.Equal(0, mediator.Calls);
    }

    [Fact]
    public async Task Register_MapsValidationFailureToBadRequest()
    {
        var mediator = new StubMediator
        {
            Handler = (_, _) => throw new TargetInvocationException(
                new ValidationFailedException(
                [
                    new ValidationFailure("Name", DomainErrorCodes.PackageNameRequired, "Package name is required.")
                ]))
        };

        var result = await PackageEndpointHandlers.RegisterAsync(
            CreateRegistrationRequest(),
            mediator,
            CreateAccessor("publisher"),
            CancellationToken.None);

        var problem = Assert.IsType<ProblemHttpResult>(result);
        Assert.Equal(400, problem.ProblemDetails.Status);
        Assert.Equal("request.validation.failed", problem.ProblemDetails.Extensions["code"]);
        Assert.True(problem.ProblemDetails.Extensions.ContainsKey("errors"));
    }

    [Fact]
    public async Task Register_MapsAuthorizationDenialToForbidden()
    {
        var mediator = new StubMediator
        {
            Handler = (_, _) => throw new TargetInvocationException(
                new AuthorizationDeniedException(nameof(PackageAction.Publish), "not permitted"))
        };

        var result = await PackageEndpointHandlers.RegisterAsync(
            CreateRegistrationRequest(),
            mediator,
            CreateAccessor("publisher"),
            CancellationToken.None);

        var problem = Assert.IsType<ProblemHttpResult>(result);
        Assert.Equal(403, problem.ProblemDetails.Status);
        Assert.Equal("authorization.denied", problem.ProblemDetails.Extensions["code"]);
    }

    [Fact]
    public async Task Build_MapsScheduledSafeBuildToAccepted()
    {
        var mediator = new StubMediator
        {
            Handler = (_, _) => ValueTask.FromResult<object>(
                new RequestBuildResult("widget", "1.2.3", "NotExecuted", "safe default", false))
        };

        var result = await PackageEndpointHandlers.RequestBuildAsync(
            "widget",
            "1.2.3",
            mediator,
            CreateAccessor("builder"),
            CancellationToken.None);

        var accepted = Assert.IsType<Accepted<BuildResponse>>(result);
        Assert.Equal("NotExecuted", accepted.Value!.Status);
        Assert.False(accepted.Value.Executed);
    }

    [Fact]
    public async Task Build_MapsMissingPackageToNotFound()
    {
        var mediator = new StubMediator
        {
            Handler = (_, _) => throw new PackageNotFoundException("widget@1.2.3")
        };

        var result = await PackageEndpointHandlers.RequestBuildAsync(
            "widget",
            "1.2.3",
            mediator,
            CreateAccessor("builder"),
            CancellationToken.None);

        var problem = Assert.IsType<ProblemHttpResult>(result);
        Assert.Equal(404, problem.ProblemDetails.Status);
    }

    [Fact]
    public async Task Build_MapsDuplicateBuildRequestToConflict()
    {
        var mediator = new StubMediator
        {
            Handler = (_, _) => throw new TargetInvocationException(
                new DomainException(
                    DomainErrorCodes.BuildAlreadyRequested,
                    "A build has already been requested."))
        };

        var result = await PackageEndpointHandlers.RequestBuildAsync(
            "widget",
            "1.2.3",
            mediator,
            CreateAccessor("builder"),
            CancellationToken.None);

        var problem = Assert.IsType<ProblemHttpResult>(result);
        Assert.Equal(409, problem.ProblemDetails.Status);
    }

    [Fact]
    public void RegistrationRequest_ExposesNoShellOrCommandField()
    {
        var propertyNames = typeof(RegisterPackageRequest)
            .GetProperties()
            .Select(property => property.Name)
            .ToArray();

        Assert.DoesNotContain(propertyNames, name => name.Contains("Command", StringComparison.OrdinalIgnoreCase));
        Assert.DoesNotContain(propertyNames, name => name.Contains("Shell", StringComparison.OrdinalIgnoreCase));
        Assert.DoesNotContain(propertyNames, name => name.Contains("Script", StringComparison.OrdinalIgnoreCase));
        Assert.DoesNotContain(propertyNames, name => name.Contains("Executable", StringComparison.OrdinalIgnoreCase));
    }

    private static RegisterPackageRequest CreateRegistrationRequest() => new(
        "widget",
        "1.2.3",
        TestPackages.SourceUrlValue,
        TestPackages.CommitSha,
        "gcc",
        "13.2.0",
        TestPackages.ContentHashValue,
        "ci://pkgsrv/tests/42",
        null,
        null,
        "Container",
        false,
        300);

    private static PackageIndexEntry SummaryEntry() => new(
        TestPackages.CreateManifest(),
        PackageState.Registered,
        TestPackages.Timestamp);

    private static readonly Guid AccountId = Guid.Parse("11111111-2222-3333-4444-555555555555");

    private static IActorContextAccessor CreateAccessor(
        string actor,
        string? roles = null,
        Guid? accountId = null)
    {
        var httpContext = new DefaultHttpContext();
        httpContext.Request.Headers[HeaderActorContextAccessor.ActorHeader] = actor;

        if (roles is not null)
        {
            httpContext.Request.Headers[HeaderActorContextAccessor.RolesHeader] = roles;
        }

        if (accountId is not null)
        {
            httpContext.Request.Headers[HeaderActorContextAccessor.AccountHeader] = accountId.Value.ToString();
        }

        return new HeaderActorContextAccessor(new HttpContextAccessor
        {
            HttpContext = httpContext
        });
    }
}
