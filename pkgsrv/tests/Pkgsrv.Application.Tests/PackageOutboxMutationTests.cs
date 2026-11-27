using Pkgsrv.Application.Features.Packages.Register;
using Pkgsrv.Application.Features.Packages.RequestBuild;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Application.Tests;

public sealed class PackageOutboxMutationTests
{
    [Fact]
    public async Task Register_OutboxMode_CommitsPackageAndEventsWithoutSynchronousPublication()
    {
        var repository = new TestOutboxRepository();
        var publisher = new ThrowingEventPublisher();
        var invalidator = new RecordingPackageCacheInvalidationNotifier();
        var handler = CreateRegistrationHandler(repository, publisher, invalidator);

        var result = await handler.Handle(
            TestPackages.CreateRegistrationCommand(),
            CancellationToken.None);

        Assert.Equal("widget", result.Package.Name);
        Assert.NotNull(await repository.FindAsync(
            PackageKey.Create("widget", "1.2.3"),
            CancellationToken.None));
        var outboxed = Assert.Single(repository.Outboxed);
        Assert.IsType<PackageRegistered>(Assert.Single(outboxed.Events));
        Assert.Equal(1, invalidator.Calls);
        Assert.Empty(publisher.Attempted);
    }

    [Fact]
    public async Task Register_OutboxFailure_DoesNotCommitPackageOrInvalidateCache()
    {
        var repository = new TestOutboxRepository { ThrowOnOutbox = true };
        var invalidator = new RecordingPackageCacheInvalidationNotifier();
        var handler = CreateRegistrationHandler(
            repository,
            new ThrowingEventPublisher(),
            invalidator);

        await Assert.ThrowsAsync<InvalidOperationException>(
            async () => await handler.Handle(
                TestPackages.CreateRegistrationCommand(),
                CancellationToken.None));

        Assert.Null(await repository.FindAsync(
            PackageKey.Create("widget", "1.2.3"),
            CancellationToken.None));
        Assert.Empty(repository.Outboxed);
        Assert.Equal(0, invalidator.Calls);
    }

    [Fact]
    public async Task Build_OutboxMode_CommitsStateAndEventsWithoutSynchronousPublication()
    {
        var repository = new TestOutboxRepository();
        var package = TestPackages.CreatePackage();
        await repository.TryAddAsync(package, CancellationToken.None);
        package.AcknowledgeDomainEvents(package.PeekDomainEvents());
        var publisher = new ThrowingEventPublisher();
        var invalidator = new RecordingPackageCacheInvalidationNotifier();
        var handler = CreateBuildHandler(
            repository,
            new TestBuildSandbox(),
            publisher,
            invalidator);

        await handler.Handle(
            new RequestPackageBuildCommand("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        var stored = await repository.FindAsync(package.Key, CancellationToken.None);
        Assert.NotNull(stored);
        Assert.Equal(PackageState.BuildRequested, stored.State);
        var outboxed = Assert.Single(repository.Outboxed);
        Assert.IsType<PackageBuildRequested>(Assert.Single(outboxed.Events));
        Assert.Equal(1, invalidator.Calls);
        Assert.Empty(publisher.Attempted);
    }

    [Fact]
    public async Task Build_OutboxFailure_DoesNotInvalidateCache()
    {
        var repository = new TestOutboxRepository { ThrowOnOutbox = true };
        var package = TestPackages.CreatePackage();
        await repository.TryAddAsync(package, CancellationToken.None);
        package.AcknowledgeDomainEvents(package.PeekDomainEvents());
        var invalidator = new RecordingPackageCacheInvalidationNotifier();
        var handler = CreateBuildHandler(
            repository,
            new TestBuildSandbox(),
            new ThrowingEventPublisher(),
            invalidator);

        await Assert.ThrowsAsync<InvalidOperationException>(
            async () => await handler.Handle(
                new RequestPackageBuildCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));

        Assert.Empty(repository.Outboxed);
        Assert.Equal(0, invalidator.Calls);
    }

    private static RegisterPackageCommandHandler CreateRegistrationHandler(
        TestOutboxRepository repository,
        Pkgsrv.Application.Interfaces.Events.IDomainEventPublisher publisher,
        RecordingPackageCacheInvalidationNotifier invalidator) =>
        new(
            repository,
            new TestSourceAllowlist(),
            new TestAuthorization(),
            publisher,
            new TestClock(),
            invalidator,
            repository);

    private static RequestPackageBuildCommandHandler CreateBuildHandler(
        TestOutboxRepository repository,
        Pkgsrv.Application.Interfaces.Builds.IBuildSandbox buildSandbox,
        Pkgsrv.Application.Interfaces.Events.IDomainEventPublisher publisher,
        RecordingPackageCacheInvalidationNotifier invalidator) =>
        new(
            repository,
            buildSandbox,
            new TestAuthorization(),
            publisher,
            new TestClock(),
            invalidator,
            repository);
}
