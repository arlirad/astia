using Pkgsrv.Application.Abstractions.Security;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Features.Packages.RequestBuild;
using Pkgsrv.Domain.Errors;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Application.Tests;

public sealed class RequestPackageBuildCommandHandlerTests
{
    [Fact]
    public async Task Build_AuthorizesAndReturnsSafeNotExecutedResult()
    {
        var repository = new TestRepository();
        var authorization = new TestAuthorization();
        var sandbox = new TestBuildSandbox();
        var events = new TestEventPublisher();
        var package = TestPackages.CreatePackage();
        await repository.TryAddAsync(package, CancellationToken.None);
        package.DequeueDomainEvents();
        var handler = new RequestPackageBuildCommandHandler(
            repository,
            sandbox,
            authorization,
            events,
            new TestClock());

        var result = await handler.Handle(
            new RequestPackageBuildCommand("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal(PackageAction.Build, Assert.Single(authorization.Actions));
        Assert.Equal("NotExecuted", result.Status);
        Assert.False(result.Executed);
        Assert.NotNull(sandbox.LastRequest);
        Assert.Equal(BuildIsolation.Container, sandbox.LastRequest.Policy.Isolation);
        Assert.IsType<PackageBuildRequested>(Assert.Single(events.Events));

        var stored = await repository.FindAsync(
            PackageKey.Create("widget", "1.2.3"),
            CancellationToken.None);
        Assert.Equal(PackageState.BuildRequested, stored!.State);
    }

    [Fact]
    public async Task Build_DeniesWhenBuildIsNotAuthorized()
    {
        var repository = new TestRepository();
        var authorization = new TestAuthorization { Allow = false };
        await repository.TryAddAsync(TestPackages.CreatePackage(), CancellationToken.None);
        var handler = new RequestPackageBuildCommandHandler(
            repository,
            new TestBuildSandbox(),
            authorization,
            new TestEventPublisher(),
            new TestClock());

        await Assert.ThrowsAsync<AuthorizationDeniedException>(
            async () => await handler.Handle(
                new RequestPackageBuildCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    [Fact]
    public async Task Build_ReturnsNotFoundForUnknownPackage()
    {
        var handler = new RequestPackageBuildCommandHandler(
            new TestRepository(),
            new TestBuildSandbox(),
            new TestAuthorization(),
            new TestEventPublisher(),
            new TestClock());

        await Assert.ThrowsAsync<PackageNotFoundException>(
            async () => await handler.Handle(
                new RequestPackageBuildCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    [Fact]
    public async Task Build_RejectsDuplicateBuildRequest()
    {
        var repository = new TestRepository();
        await repository.TryAddAsync(TestPackages.CreatePackage(), CancellationToken.None);
        var handler = new RequestPackageBuildCommandHandler(
            repository,
            new TestBuildSandbox(),
            new TestAuthorization(),
            new TestEventPublisher(),
            new TestClock());
        var command = new RequestPackageBuildCommand("widget", "1.2.3", TestPackages.Actor);

        await handler.Handle(command, CancellationToken.None);

        var exception = await Assert.ThrowsAsync<DomainException>(
            async () => await handler.Handle(command, CancellationToken.None));
        Assert.Equal(DomainErrorCodes.BuildAlreadyRequested, exception.Code);
    }
}
