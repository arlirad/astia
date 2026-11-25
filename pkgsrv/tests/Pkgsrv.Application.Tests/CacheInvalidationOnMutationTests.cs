using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Features.Packages.Register;
using Pkgsrv.Application.Features.Packages.RequestBuild;
using Pkgsrv.Application.Interfaces.Events;
using Pkgsrv.Domain.Packages;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Application.Tests;

public sealed class CacheInvalidationOnMutationTests
{
    [Fact]
    public async Task Register_PublicationFailure_InvalidatesCommittedMutation()
    {
        var repository = new TestRepository();
        var invalidator = new RecordingPackageCacheInvalidator();
        var handler = CreateRegistrationHandler(
            repository,
            new ThrowingEventPublisher(),
            invalidator);

        await Assert.ThrowsAsync<InvalidOperationException>(
            async () => await handler.Handle(
                TestPackages.CreateRegistrationCommand(),
                CancellationToken.None));

        Assert.Equal(1, invalidator.Calls);
        Assert.NotNull(await repository.FindAsync(
            PackageKey.Create("widget", "1.2.3"),
            CancellationToken.None));
    }

    [Fact]
    public async Task Register_AcknowledgementFailure_InvalidatesCommittedMutation()
    {
        var repository = new TestRepository { ThrowOnAcknowledge = true };
        var invalidator = new RecordingPackageCacheInvalidator();
        var handler = CreateRegistrationHandler(
            repository,
            new TestEventPublisher(),
            invalidator);

        await Assert.ThrowsAsync<InvalidOperationException>(
            async () => await handler.Handle(
                TestPackages.CreateRegistrationCommand(),
                CancellationToken.None));

        Assert.Equal(1, invalidator.Calls);
    }

    [Fact]
    public async Task Register_Conflict_DoesNotInvalidate()
    {
        var repository = new TestRepository();
        await repository.TryAddAsync(TestPackages.CreatePackage(), CancellationToken.None);
        var invalidator = new RecordingPackageCacheInvalidator();
        var handler = CreateRegistrationHandler(
            repository,
            new TestEventPublisher(),
            invalidator);

        await Assert.ThrowsAsync<PackageConflictException>(
            async () => await handler.Handle(
                TestPackages.CreateRegistrationCommand(),
                CancellationToken.None));

        Assert.Equal(0, invalidator.Calls);
    }

    [Fact]
    public async Task Build_PublicationFailure_InvalidatesCommittedState()
    {
        var repository = new TestRepository();
        var package = TestPackages.CreatePackage();
        await repository.TryAddAsync(package, CancellationToken.None);
        package.AcknowledgeDomainEvents(package.PeekDomainEvents());
        var invalidator = new RecordingPackageCacheInvalidator();
        var handler = CreateBuildHandler(
            repository,
            new TestBuildSandbox(),
            new ThrowingEventPublisher(),
            invalidator);

        await Assert.ThrowsAsync<InvalidOperationException>(
            async () => await handler.Handle(
                new RequestPackageBuildCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));

        Assert.Equal(1, invalidator.Calls);
        Assert.Equal(PackageState.BuildRequested, package.State);
    }

    [Fact]
    public async Task Build_AcknowledgementFailure_InvalidatesCommittedState()
    {
        var repository = new TestRepository { ThrowOnAcknowledge = true };
        var package = TestPackages.CreatePackage();
        await repository.TryAddAsync(package, CancellationToken.None);
        package.AcknowledgeDomainEvents(package.PeekDomainEvents());
        var invalidator = new RecordingPackageCacheInvalidator();
        var handler = CreateBuildHandler(
            repository,
            new TestBuildSandbox(),
            new TestEventPublisher(),
            invalidator);

        await Assert.ThrowsAsync<InvalidOperationException>(
            async () => await handler.Handle(
                new RequestPackageBuildCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));

        Assert.Equal(1, invalidator.Calls);
    }

    [Fact]
    public async Task Build_SandboxFailure_InvalidatesCommittedState()
    {
        var repository = new TestRepository();
        var package = TestPackages.CreatePackage();
        await repository.TryAddAsync(package, CancellationToken.None);
        package.AcknowledgeDomainEvents(package.PeekDomainEvents());
        var invalidator = new RecordingPackageCacheInvalidator();
        var handler = CreateBuildHandler(
            repository,
            new ThrowingBuildSandbox(),
            new TestEventPublisher(),
            invalidator);

        await Assert.ThrowsAsync<InvalidOperationException>(
            async () => await handler.Handle(
                new RequestPackageBuildCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));

        Assert.Equal(1, invalidator.Calls);
        Assert.Equal(PackageState.BuildRequested, package.State);
    }

    [Fact]
    public async Task Build_UpdateFailure_DoesNotInvalidate()
    {
        var repository = new TestRepository { ThrowOnUpdate = true };
        var package = TestPackages.CreatePackage();
        await repository.TryAddAsync(package, CancellationToken.None);
        package.AcknowledgeDomainEvents(package.PeekDomainEvents());
        var invalidator = new RecordingPackageCacheInvalidator();
        var handler = CreateBuildHandler(
            repository,
            new TestBuildSandbox(),
            new TestEventPublisher(),
            invalidator);

        await Assert.ThrowsAsync<PackageConflictException>(
            async () => await handler.Handle(
                new RequestPackageBuildCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));

        Assert.Equal(0, invalidator.Calls);
    }

    private static RegisterPackageCommandHandler CreateRegistrationHandler(
        TestRepository repository,
        IDomainEventPublisher publisher,
        RecordingPackageCacheInvalidator invalidator) =>
        new(
            repository,
            new TestSourceAllowlist(),
            new TestAuthorization(),
            publisher,
            new TestClock(),
            invalidator);

    private static RequestPackageBuildCommandHandler CreateBuildHandler(
        TestRepository repository,
        Pkgsrv.Application.Interfaces.Builds.IBuildSandbox buildSandbox,
        IDomainEventPublisher publisher,
        RecordingPackageCacheInvalidator invalidator) =>
        new(
            repository,
            buildSandbox,
            new TestAuthorization(),
            publisher,
            new TestClock(),
            invalidator);
}
