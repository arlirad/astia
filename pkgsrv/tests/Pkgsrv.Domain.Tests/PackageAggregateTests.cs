using Pkgsrv.Domain.Errors;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Domain.Tests;

public sealed class PackageAggregateTests
{
    [Fact]
    public void Register_StartsInRegisteredStateAndRaisesEvent()
    {
        var package = TestPackages.CreatePackage();

        Assert.Equal(PackageState.Registered, package.State);
        Assert.Equal(TestPackages.Timestamp, package.RegisteredAt);
        Assert.Null(package.BuildRequestedAt);

        var domainEvent = Assert.IsType<PackageRegistered>(Assert.Single(package.DomainEvents));
        Assert.Equal(package.Key, domainEvent.Key);
        Assert.Equal(TestPackages.Timestamp, domainEvent.OccurredAt);
    }

    [Fact]
    public void RequestBuild_TransitionsOnceAndRaisesEvent()
    {
        var package = TestPackages.CreatePackage();
        var requestedAt = TestPackages.Timestamp.AddMinutes(5);

        package.RequestBuild(requestedAt);

        Assert.Equal(PackageState.BuildRequested, package.State);
        Assert.Equal(requestedAt, package.BuildRequestedAt);

        Assert.Contains(package.DomainEvents, domainEvent => domainEvent is PackageBuildRequested);
    }

    [Fact]
    public void RequestBuild_RejectsDuplicateRequest()
    {
        var package = TestPackages.CreatePackage();
        package.RequestBuild(TestPackages.Timestamp);

        var exception = Assert.Throws<DomainException>(
            () => package.RequestBuild(TestPackages.Timestamp.AddMinutes(1)));

        Assert.Equal(DomainErrorCodes.BuildAlreadyRequested, exception.Code);
    }

    [Fact]
    public void AcknowledgeDomainEvents_RemovesPublishedSnapshot()
    {
        var package = TestPackages.CreatePackage();
        var events = package.PeekDomainEvents();

        Assert.Single(events);

        package.AcknowledgeDomainEvents(events);

        Assert.Empty(package.DomainEvents);
    }

    [Fact]
    public void Rehydrate_RestoresRegisteredStateWithoutEvents()
    {
        var package = Package.Rehydrate(
            TestPackages.CreateManifest(TestPackages.CreateSignedSignature()),
            BuildPolicy.Create(BuildIsolation.Container, false, TimeSpan.FromMinutes(5)),
            PackageState.Registered,
            TestPackages.Timestamp,
            null);

        Assert.Equal(PackageState.Registered, package.State);
        Assert.Null(package.BuildRequestedAt);
        Assert.True(package.Manifest.IsPublishable);
        Assert.Empty(package.DomainEvents);
    }

    [Fact]
    public void Rehydrate_RestoresBuildRequestedStateWithoutEvents()
    {
        var requestedAt = TestPackages.Timestamp.AddMinutes(5);
        var package = Package.Rehydrate(
            TestPackages.CreateManifest(),
            BuildPolicy.Create(BuildIsolation.Namespace, true, TimeSpan.FromMinutes(10)),
            PackageState.BuildRequested,
            TestPackages.Timestamp,
            requestedAt);

        Assert.Equal(PackageState.BuildRequested, package.State);
        Assert.Equal(requestedAt, package.BuildRequestedAt);
        Assert.True(package.BuildPolicy.AllowNetwork);
        Assert.Empty(package.DomainEvents);
    }

    [Fact]
    public void MarkPublished_TransitionsBuildRequestedSignedPackage()
    {
        var package = TestPackages.CreatePackage(signed: true);
        package.AcknowledgeDomainEvents(package.PeekDomainEvents());
        package.RequestBuild(TestPackages.Timestamp);
        var publishedAt = TestPackages.Timestamp.AddMinutes(3);

        package.EnsureCanPublish();
        package.MarkPublished(publishedAt);

        Assert.Equal(PackageState.Published, package.State);
        Assert.Equal(publishedAt, package.PublishedAt);
        Assert.Equal(TestPackages.Timestamp, package.BuildRequestedAt);
    }

    [Fact]
    public void MarkPublished_RequiresSignedBuildRequestedPackage()
    {
        var registered = TestPackages.CreatePackage(signed: true);
        Assert.Throws<DomainException>(() => registered.MarkPublished(TestPackages.Timestamp));

        var pending = TestPackages.CreatePackage();
        pending.RequestBuild(TestPackages.Timestamp);
        var exception = Assert.Throws<DomainException>(
            () => pending.MarkPublished(TestPackages.Timestamp));
        Assert.Equal(DomainErrorCodes.SignaturePending, exception.Code);

        var published = TestPackages.CreatePackage(signed: true);
        published.AcknowledgeDomainEvents(published.PeekDomainEvents());
        published.RequestBuild(TestPackages.Timestamp);
        published.MarkPublished(TestPackages.Timestamp);
        var stateException = Assert.Throws<DomainException>(
            () => published.RequestBuild(TestPackages.Timestamp));
        Assert.Equal(DomainErrorCodes.PackageStateInvalid, stateException.Code);
    }

    [Fact]
    public void Rehydrate_RestoresPublishedStateAndTimestamp()
    {
        var publishedAt = TestPackages.Timestamp.AddMinutes(4);
        var package = Package.Rehydrate(
            TestPackages.CreateManifest(TestPackages.CreateSignedSignature()),
            BuildPolicy.Create(BuildIsolation.Container, false, TimeSpan.FromMinutes(5)),
            PackageState.Published,
            TestPackages.Timestamp,
            TestPackages.Timestamp.AddMinutes(1),
            publishedAt: publishedAt);

        Assert.Equal(PackageState.Published, package.State);
        Assert.Equal(publishedAt, package.PublishedAt);
        Assert.Empty(package.DomainEvents);
    }

    [Fact]
    public void Rehydrate_RejectsPublishedStateWithoutPublicationTimestamp()
    {
        var exception = Assert.Throws<DomainException>(() => Package.Rehydrate(
            TestPackages.CreateManifest(TestPackages.CreateSignedSignature()),
            BuildPolicy.Create(BuildIsolation.Container, false, TimeSpan.FromMinutes(5)),
            PackageState.Published,
            TestPackages.Timestamp,
            TestPackages.Timestamp.AddMinutes(1)));

        Assert.Equal(DomainErrorCodes.PackageStateInvalid, exception.Code);
    }

    [Theory]
    [InlineData(PackageState.Registered, true)]
    [InlineData(PackageState.BuildRequested, false)]
    public void Rehydrate_RejectsInconsistentState(PackageState state, bool hasBuildTimestamp)
    {
        var buildRequestedAt = hasBuildTimestamp ? TestPackages.Timestamp : (DateTimeOffset?)null;

        var exception = Assert.Throws<DomainException>(() => Package.Rehydrate(
            TestPackages.CreateManifest(),
            BuildPolicy.Create(BuildIsolation.Container, false, TimeSpan.FromMinutes(5)),
            state,
            TestPackages.Timestamp,
            buildRequestedAt));

        Assert.Equal(DomainErrorCodes.PackageStateInvalid, exception.Code);
    }

    [Fact]
    public void AcknowledgeDomainEvents_LeavesEventsAddedAfterSnapshot()
    {
        var package = TestPackages.CreatePackage();
        var published = package.PeekDomainEvents();
        package.RequestBuild(TestPackages.Timestamp);

        package.AcknowledgeDomainEvents(published);

        var remaining = Assert.Single(package.DomainEvents);
        Assert.IsType<PackageBuildRequested>(remaining);
    }

    [Fact]
    public async Task RequestBuild_AllowsExactlyOneConcurrentTransition()
    {
        var package = TestPackages.CreatePackage();
        package.AcknowledgeDomainEvents(package.PeekDomainEvents());
        var start = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        var attempts = Enumerable.Range(0, 64)
            .Select(_ => Task.Run(async () =>
            {
                await start.Task;

                try
                {
                    package.RequestBuild(TestPackages.Timestamp);
                    return true;
                }
                catch (DomainException exception) when (exception.Code == DomainErrorCodes.BuildAlreadyRequested)
                {
                    return false;
                }
            }))
            .ToArray();

        start.SetResult();
        var outcomes = await Task.WhenAll(attempts);

        Assert.Equal(1, outcomes.Count(outcome => outcome));
        Assert.Equal(63, outcomes.Count(outcome => !outcome));
        Assert.Equal(PackageState.BuildRequested, package.State);
        Assert.Equal(TestPackages.Timestamp, package.BuildRequestedAt);

        var buildEvent = Assert.Single(package.DomainEvents.OfType<PackageBuildRequested>());
        Assert.Equal(TestPackages.Timestamp, buildEvent.OccurredAt);
    }
}
