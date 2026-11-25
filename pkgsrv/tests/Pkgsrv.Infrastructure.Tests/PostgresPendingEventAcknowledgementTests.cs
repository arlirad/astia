using Pkgsrv.Application.Errors;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Persistence.Postgres;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPendingEventAcknowledgementTests
{
    [Fact]
    public void SuccessfulPublication_ClearsPersistedPendingEventBySemanticIdentity()
    {
        var package = TestPackages.CreatePackage();
        var record = PostgresPackageMapper.FromDomain(package);
        var rehydrated = PostgresPackageMapper.ToDomain(record);
        Assert.Single(rehydrated.PeekDomainEvents());

        var publishedEvents = package.PeekDomainEvents();
        record.PendingEventsJson = PostgresPendingEventSerializer.RemoveAcknowledged(
            record.PendingEventsJson,
            publishedEvents);

        var afterAcknowledgement = PostgresPackageMapper.ToDomain(record);

        Assert.Empty(afterAcknowledgement.PeekDomainEvents());
        Assert.Equal("[]", record.PendingEventsJson);
    }

    [Fact]
    public void FailedPublication_LeavesPendingEventDurableAcrossRestart()
    {
        var package = TestPackages.CreatePackage();
        var record = PostgresPackageMapper.FromDomain(package);

        var afterRestart = PostgresPackageMapper.ToDomain(record);

        var pending = Assert.IsType<PackageRegistered>(
            Assert.Single(afterRestart.PeekDomainEvents()));
        Assert.Equal(package.Key, pending.Key);
    }

    [Fact]
    public void Acknowledgement_PreservesUnpublishedPendingEvents()
    {
        var package = TestPackages.CreatePackage();
        package.RequestBuild(TestPackages.Timestamp.AddMinutes(1));
        var buildRequested = Assert.Single(package.PeekDomainEvents().OfType<PackageBuildRequested>());
        var record = PostgresPackageMapper.FromDomain(package);

        record.PendingEventsJson = PostgresPendingEventSerializer.RemoveAcknowledged(
            record.PendingEventsJson,
            [buildRequested]);

        var afterAcknowledgement = PostgresPackageMapper.ToDomain(record);

        var pending = Assert.IsType<PackageRegistered>(
            Assert.Single(afterAcknowledgement.PeekDomainEvents()));
        Assert.Equal(package.Key, pending.Key);
    }

    [Fact]
    public void Acknowledgement_DoesNotRemoveDifferentPackageOrEventKind()
    {
        var registeredPackage = TestPackages.CreatePackage(name: "widget");
        var buildPackage = TestPackages.CreatePackage(name: "libc");
        buildPackage.RequestBuild(TestPackages.Timestamp);
        var combinedEvents = registeredPackage.PeekDomainEvents()
            .Concat(buildPackage.PeekDomainEvents())
            .ToArray();
        var serialized = PostgresPendingEventSerializer.Serialize(combinedEvents);

        var remaining = PostgresPendingEventSerializer.RemoveAcknowledged(
            serialized,
            registeredPackage.PeekDomainEvents());
        var rehydrated = PostgresPendingEventSerializer.Deserialize(
            remaining,
            buildPackage.Manifest,
            buildPackage.BuildPolicy);

        Assert.Equal(2, rehydrated.Count);
        Assert.Contains(rehydrated, domainEvent => domainEvent is PackageRegistered);
        Assert.Contains(rehydrated, domainEvent => domainEvent is PackageBuildRequested);
    }

    [Fact]
    public async Task Cas_RetryPreservesNewerPendingEvents()
    {
        var package = TestPackages.CreatePackage();
        var publishedEvents = package.PeekDomainEvents();
        var registered = Assert.Single(publishedEvents);
        var buildRequested = new PackageBuildRequested(
            package.Key,
            package.BuildPolicy,
            TestPackages.Timestamp.AddMinutes(1));
        var initialJson = PostgresPendingEventSerializer.Serialize(publishedEvents);
        var concurrentlyUpdatedJson = PostgresPendingEventSerializer.Serialize(
            [registered, buildRequested]);
        var store = initialJson;
        var casAttempts = 0;

        await PostgresPendingEventCas.ApplyAsync(
            package.Key,
            _ => Task.FromResult<string?>(store),
            (original, updated, _) =>
            {
                casAttempts++;

                if (casAttempts == 1)
                {
                    store = concurrentlyUpdatedJson;
                    return Task.FromResult(false);
                }

                if (!string.Equals(store, original, StringComparison.Ordinal))
                {
                    return Task.FromResult(false);
                }

                store = updated;
                return Task.FromResult(true);
            },
            publishedEvents,
            CancellationToken.None);

        Assert.Equal(2, casAttempts);

        var remaining = PostgresPendingEventSerializer.Deserialize(
            store,
            package.Manifest,
            package.BuildPolicy);
        var pending = Assert.IsType<PackageBuildRequested>(Assert.Single(remaining));
        Assert.Equal(buildRequested.OccurredAt, pending.OccurredAt);
    }

    [Fact]
    public async Task Cas_ExhaustionMapsToPackageConflict()
    {
        var package = TestPackages.CreatePackage();
        var attempts = 0;

        var exception = await Assert.ThrowsAsync<PackageConflictException>(
            () => PostgresPendingEventCas.ApplyAsync(
                package.Key,
                _ => Task.FromResult<string?>(
                    PostgresPendingEventSerializer.Serialize(package.PeekDomainEvents())),
                (_, _, _) =>
                {
                    attempts++;
                    return Task.FromResult(false);
                },
                package.PeekDomainEvents(),
                CancellationToken.None));

        Assert.Equal(PostgresPendingEventCas.MaxAttempts, attempts);
        Assert.Equal("package.conflict", exception.Code);
    }
}
