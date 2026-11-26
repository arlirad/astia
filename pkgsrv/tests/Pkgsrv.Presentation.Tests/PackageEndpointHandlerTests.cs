using System.Reflection;
using System.Text.Json;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Http.HttpResults;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.DependencyInjection;
using Pkgsrv.Application.Contracts;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Features.Packages.Download;
using Pkgsrv.Application.Features.Packages.Get;
using Pkgsrv.Application.Features.Packages.List;
using Pkgsrv.Application.Features.Packages.Publish;
using Pkgsrv.Application.Features.Packages.Register;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Validation;
using Pkgsrv.Domain.Errors;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Presentation.Api;
using Pkgsrv.Presentation.Caching;
using Pkgsrv.Presentation.Contracts;
using Pkgsrv.Presentation.Security;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Presentation.Tests;

public sealed class PackageEndpointHandlerTests
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    [Fact]
    public async Task List_MapsSuccessfulQueryAndPropagatesActorHeaders()
    {
        var mediator = new StubMediator
        {
            Handler = (_, _) => ValueTask.FromResult<object>(
                new[] { PackageSummaryDto.From(SummaryEntry()) })
        };
        var accessor = CreateAccessor("alice", "reader, builder", AccountId);

        var result = await PackageEndpointHandlers.ListAsync(
            mediator,
            accessor,
            new TestPackageJsonCache(),
            CancellationToken.None);

        var response = await ExecuteAsync(result);
        Assert.Equal(200, response.StatusCode);
        Assert.Contains("application/json", response.ContentType, StringComparison.OrdinalIgnoreCase);

        using var document = JsonDocument.Parse(response.Body);
        Assert.Equal(
            "widget",
            document.RootElement.GetProperty("packages")[0].GetProperty("name").GetString());

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
        var accessor = new HttpTransportIdentityAccessor(new HttpContextAccessor
        {
            HttpContext = new DefaultHttpContext()
        });

        await PackageEndpointHandlers.ListAsync(
            mediator,
            accessor,
            new TestPackageJsonCache(),
            CancellationToken.None);

        var query = Assert.IsType<ListPackagesQuery>(mediator.LastRequest);
        Assert.True(query.Actor.IsAnonymous);
    }

    [Fact]
    public async Task List_SecondReadUsesCachedJsonAndSkipsMediator()
    {
        var mediator = new StubMediator
        {
            Handler = (_, _) => ValueTask.FromResult<object>(
                new[] { PackageSummaryDto.From(SummaryEntry()) })
        };
        using var memory = new MemoryCache(new MemoryCacheOptions
        {
            SizeLimit = PackageJsonCacheOptions.Default.SizeLimit
        });
        var cache = new PackageJsonCache(memory, PackageJsonCacheOptions.Default, new AlwaysAllowAuthorization());
        var accessor = CreateAccessor("reader");

        await PackageEndpointHandlers.ListAsync(
            mediator,
            accessor,
            cache,
            CancellationToken.None);
        await PackageEndpointHandlers.ListAsync(
            mediator,
            accessor,
            cache,
            CancellationToken.None);

        Assert.Equal(1, mediator.Calls);
    }

    [Fact]
    public async Task Get_ReturnsPackageDetailsAndPropagatesActor()
    {
        var mediator = new StubMediator
        {
            Handler = (_, _) => ValueTask.FromResult<object>(
                PackageDetailsDto.From(TestPackages.CreatePackage()))
        };

        var result = await PackageEndpointHandlers.GetAsync(
            "widget",
            "1.2.3",
            mediator,
            CreateAccessor("reader"),
            new TestPackageJsonCache(),
            CancellationToken.None);

        var response = await ExecuteAsync(result);
        Assert.Equal(200, response.StatusCode);
        Assert.Contains("application/json", response.ContentType, StringComparison.OrdinalIgnoreCase);

        var body = JsonSerializer.Deserialize<PackageResponse>(response.Body, JsonOptions);
        Assert.Equal("widget", body!.Name);

        var query = Assert.IsType<GetPackageQuery>(mediator.LastRequest);
        Assert.Equal("reader", query.Actor.ActorId);
    }

    [Fact]
    public async Task Get_MapsMissingPackageToNotFound()
    {
        var mediator = new StubMediator
        {
            Handler = (_, _) => throw new PackageNotFoundException("widget@1.2.3")
        };

        var result = await PackageEndpointHandlers.GetAsync(
            "widget",
            "1.2.3",
            mediator,
            CreateAccessor("reader"),
            new TestPackageJsonCache(),
            CancellationToken.None);

        var problem = Assert.IsType<ProblemHttpResult>(result);
        Assert.Equal(404, problem.ProblemDetails.Status);
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
    public async Task List_MapsMissingOrInvalidBearerToUnauthorized()
    {
        var mediator = new StubMediator
        {
            Handler = (_, _) => throw new TargetInvocationException(
                new AuthenticationRequiredException(
                    nameof(PackageAction.Read),
                    "A bearer access token is required."))
        };

        var result = await PackageEndpointHandlers.ListAsync(
            mediator,
            CreateAccessor("anonymous"),
            new TestPackageJsonCache(),
            CancellationToken.None);

        var problem = Assert.IsType<ProblemHttpResult>(result);
        Assert.Equal(401, problem.ProblemDetails.Status);
        Assert.Equal("authorization.unauthenticated", problem.ProblemDetails.Extensions["code"]);
    }

    [Fact]
    public async Task List_MapsVerifiedPermissionDenialToForbidden()
    {
        var mediator = new StubMediator
        {
            Handler = (_, _) => throw new TargetInvocationException(
                new AuthorizationDeniedException(
                    nameof(PackageAction.Read),
                    "Irva verified account lacks permission 'read'."))
        };

        var result = await PackageEndpointHandlers.ListAsync(
            mediator,
            CreateAccessor("verified-reader"),
            new TestPackageJsonCache(),
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
    public async Task Build_MapsStaleRepositoryTransitionToConflict()
    {
        var mediator = new StubMediator
        {
            Handler = (_, _) => throw new PackageConflictException(
                "widget@1.2.3",
                "Package widget@1.2.3 was modified by another request.")
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
    public async Task Publish_ReturnsOkAndPassesActor()
    {
        var mediator = new StubMediator
        {
            Handler = (_, _) => ValueTask.FromResult<object>(
                new PublishPackageResult("widget", "1.2.3", TestPackages.ContentHashValue, "test-signature"))
        };

        var result = await PackageEndpointHandlers.PublishAsync(
            "widget",
            "1.2.3",
            mediator,
            CreateAccessor("publisher"),
            CancellationToken.None);

        var ok = Assert.IsType<Ok<PublishResponse>>(result);
        Assert.Equal("test-signature", ok.Value!.Signature);
        var command = Assert.IsType<PublishPackageCommand>(mediator.LastRequest);
        Assert.Equal("publisher", command.Actor.ActorId);
    }

    [Fact]
    public async Task Download_ReturnsBinaryContent()
    {
        var content = "artifact"u8.ToArray();
        var mediator = new StubMediator
        {
            Handler = (_, _) => ValueTask.FromResult<object>(
                new PackageDownloadResult(
                    "widget",
                    "1.2.3",
                    TestPackages.ContentHashValue,
                    content))
        };

        var result = await PackageEndpointHandlers.DownloadAsync(
            "widget",
            "1.2.3",
            mediator,
            CreateAccessor("reader"),
            CancellationToken.None);
        var response = await ExecuteAsync(result);

        Assert.Equal(200, response.StatusCode);
        Assert.Contains("application/octet-stream", response.ContentType, StringComparison.OrdinalIgnoreCase);
        Assert.Equal("artifact", response.Body);
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
        var roleValues = string.IsNullOrWhiteSpace(roles)
            ? Array.Empty<string>()
            : roles.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

        return new StubActorContextAccessor(
            AuthorizationContext.Create(actor, roleValues, accountId));
    }

    private sealed class StubActorContextAccessor : IActorContextAccessor
    {
        private readonly AuthorizationContext _actor;

        public StubActorContextAccessor(AuthorizationContext actor)
        {
            _actor = actor;
        }

        public AuthorizationContext GetCurrent() => _actor;
    }

    private static async Task<(int StatusCode, string ContentType, string Body)> ExecuteAsync(IResult result)
    {
        var httpContext = new DefaultHttpContext
        {
            RequestServices = new ServiceCollection().AddLogging().BuildServiceProvider()
        };
        httpContext.Response.Body = new MemoryStream();

        await result.ExecuteAsync(httpContext);

        httpContext.Response.Body.Position = 0;
        using var reader = new StreamReader(httpContext.Response.Body);
        var body = await reader.ReadToEndAsync();

        return (httpContext.Response.StatusCode, httpContext.Response.ContentType ?? string.Empty, body);
    }

    private sealed class AlwaysAllowAuthorization : IIrvaAuthorization
    {
        public Task<AuthorizationResult> AuthorizeAsync(
            AuthorizationContext context,
            PackageAction action,
            PackageKey? package,
            CancellationToken cancellationToken) =>
            Task.FromResult(AuthorizationResult.Allow());
    }

    private sealed class TestPackageJsonCache : IPackageJsonCache
    {
        public async Task<string> GetListJsonAsync(
            AuthorizationContext actor,
            string? search,
            Func<CancellationToken, Task<PackageListResponse>> load,
            CancellationToken cancellationToken) =>
            JsonSerializer.Serialize(await load(cancellationToken), JsonOptions);

        public async Task<string> GetPackageJsonAsync(
            AuthorizationContext actor,
            string name,
            string version,
            Func<CancellationToken, Task<PackageResponse>> load,
            CancellationToken cancellationToken) =>
            JsonSerializer.Serialize(await load(cancellationToken), JsonOptions);
    }
}
