using Microsoft.EntityFrameworkCore;
using Pkgsrv.Domain.Events;
using Pkgsrv.Infrastructure.Events.Outbox;
using Pkgsrv.Infrastructure.Persistence.Postgres;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPackageOutboxIntegrationTests
{
    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task TryAddWithEvents_CommitsPackageAndOutboxAtomically()
    {
        await using var database = await PostgresTestDatabase.CreateAsync(CancellationToken.None);
        await using (var migrationContext = database.CreateContext())
        {
            await migrationContext.Database.MigrateAsync(CancellationToken.None);
        }

        var repository = new PostgresPackageRepository(database.CreateFactory());
        var package = TestPackages.CreatePackage(name: $"outbox-{Guid.NewGuid():N}");
        var domainEvents = package.PeekDomainEvents();
        var expected = PostgresOutboxEventSerializer.ToOutboxEvent(
            Assert.Single(domainEvents));

        Assert.True(await repository.TryAddWithEventsAsync(
            package,
            domainEvents,
            CancellationToken.None));

        Assert.NotNull(await repository.FindAsync(package.Key, CancellationToken.None));

        await using var context = database.CreateContext();
        var outboxRecord = await context.PackageOutbox
            .AsNoTracking()
            .SingleAsync(CancellationToken.None);

        Assert.Equal(expected.IdempotencyKey, outboxRecord.IdempotencyKey);
        Assert.Equal(package.Key.Name.Value, outboxRecord.PackageName);
        Assert.Equal(package.Key.Version.ToString(), outboxRecord.PackageVersion);
        Assert.Equal(expected.EventType, outboxRecord.EventType);
        Assert.Equal(expected.Payload, outboxRecord.Payload);
        Assert.Equal(expected.OccurredAt, outboxRecord.OccurredAt);
        Assert.Equal((int)PackageOutboxStatus.Pending, outboxRecord.Status);
        Assert.Equal(0, outboxRecord.AttemptCount);
        Assert.Null(outboxRecord.ClaimedAt);
        Assert.Null(outboxRecord.ClaimId);
        Assert.Null(outboxRecord.LastError);
    }

    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task TryAddWithEvents_RollsBackPackageWhenOutboxWriteFails()
    {
        await using var database = await PostgresTestDatabase.CreateAsync(CancellationToken.None);
        await using (var migrationContext = database.CreateContext())
        {
            await migrationContext.Database.MigrateAsync(CancellationToken.None);
        }

        var repository = new PostgresPackageRepository(database.CreateFactory());
        var package = TestPackages.CreatePackage(name: $"rollback-{Guid.NewGuid():N}");

        await Assert.ThrowsAsync<InvalidOperationException>(
            () => repository.TryAddWithEventsAsync(
                package,
                [new UnsupportedDomainEvent(TestPackages.Timestamp)],
                CancellationToken.None));

        Assert.Null(await repository.FindAsync(package.Key, CancellationToken.None));

        await using var context = database.CreateContext();
        Assert.Equal(0, await context.Packages.CountAsync(CancellationToken.None));
        Assert.Equal(0, await context.PackageOutbox.CountAsync(CancellationToken.None));
    }

    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task DuplicateIdempotencyKey_DoesNotCreateDuplicateOutboxRows()
    {
        await using var database = await PostgresTestDatabase.CreateAsync(CancellationToken.None);
        await using (var migrationContext = database.CreateContext())
        {
            await migrationContext.Database.MigrateAsync(CancellationToken.None);
        }

        var repository = new PostgresPackageRepository(database.CreateFactory());
        var package = TestPackages.CreatePackage(name: $"dedupe-{Guid.NewGuid():N}");
        var domainEvents = package.PeekDomainEvents();

        Assert.True(await repository.TryAddWithEventsAsync(
            package,
            domainEvents,
            CancellationToken.None));
        await repository.UpdateWithEventsAsync(
            package,
            domainEvents,
            CancellationToken.None);
        await repository.UpdateWithEventsAsync(
            package,
            domainEvents,
            CancellationToken.None);

        await using var context = database.CreateContext();
        var outboxRecord = await context.PackageOutbox
            .AsNoTracking()
            .SingleAsync(CancellationToken.None);

        Assert.Equal(
            PostgresOutboxEventSerializer.ToOutboxEvent(Assert.Single(domainEvents))
                .IdempotencyKey,
            outboxRecord.IdempotencyKey);
    }

    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task OutboxStore_ClaimsRetriesAndReplaysAfterRestart()
    {
        await using var database = await PostgresTestDatabase.CreateAsync(CancellationToken.None);
        await using (var migrationContext = database.CreateContext())
        {
            await migrationContext.Database.MigrateAsync(CancellationToken.None);
        }

        var factory = database.CreateFactory();
        var repository = new PostgresPackageRepository(factory);
        var package = TestPackages.CreatePackage(name: $"claim-{Guid.NewGuid():N}");
        Assert.True(await repository.TryAddWithEventsAsync(
            package,
            package.PeekDomainEvents(),
            CancellationToken.None));

        var claimTime = DateTimeOffset.UtcNow.AddMinutes(1);
        var lease = TimeSpan.FromMinutes(1);
        var store = new PostgresPackageOutboxStore(factory);

        var firstClaim = Assert.Single(await store.ClaimAsync(
            claimTime,
            10,
            lease,
            CancellationToken.None));
        Assert.Equal(1, firstClaim.AttemptCount);
        Assert.False(string.IsNullOrWhiteSpace(firstClaim.ClaimId));

        Assert.Empty(await store.ClaimAsync(
            claimTime,
            10,
            lease,
            CancellationToken.None));

        var retryAt = claimTime + TimeSpan.FromSeconds(5);
        await store.MarkFailedAsync(
            firstClaim.IdempotencyKey,
            firstClaim.ClaimId,
            firstClaim.AttemptCount,
            retryAt,
            exhausted: false,
            error: "transient",
            CancellationToken.None);

        Assert.Empty(await store.ClaimAsync(
            claimTime + TimeSpan.FromSeconds(1),
            10,
            lease,
            CancellationToken.None));

        var restartedStore = new PostgresPackageOutboxStore(factory);
        var replayClaim = Assert.Single(await restartedStore.ClaimAsync(
            retryAt,
            10,
            lease,
            CancellationToken.None));
        Assert.Equal(firstClaim.IdempotencyKey, replayClaim.IdempotencyKey);
        Assert.Equal(2, replayClaim.AttemptCount);

        await restartedStore.MarkDeliveredAsync(
            replayClaim.IdempotencyKey,
            replayClaim.ClaimId,
            CancellationToken.None);

        Assert.Empty(await restartedStore.ClaimAsync(
            retryAt + TimeSpan.FromHours(1),
            10,
            lease,
            CancellationToken.None));

        await using var context = database.CreateContext();
        var outboxRecord = await context.PackageOutbox
            .AsNoTracking()
            .SingleAsync(CancellationToken.None);
        Assert.Equal((int)PackageOutboxStatus.Delivered, outboxRecord.Status);
        Assert.Equal(2, outboxRecord.AttemptCount);
        Assert.Null(outboxRecord.ClaimedAt);
        Assert.Null(outboxRecord.ClaimId);
        Assert.Null(outboxRecord.LastError);
    }

    private sealed record UnsupportedDomainEvent(DateTimeOffset OccurredAt) : IDomainEvent;
}
