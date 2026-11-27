using Microsoft.EntityFrameworkCore;
using Npgsql;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Events.Outbox;
using Pkgsrv.Infrastructure.Persistence.Postgres;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPendingEventBackfillIntegrationTests
{
    private const string PreviousMigration = "20261127055233_AddSemVerPrecedenceKey";

    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task Migration_BackfillsRegistrationAndBuildEventsAndClearsDescriptors()
    {
        await using var database = await CreateDatabaseAtPreviousMigrationAsync();
        var package = CreatePackageWithFractionalTimestamps();
        package.RequestBuild(package.RegisteredAt.AddMinutes(1).AddTicks(7_654_321));
        var events = package.PeekDomainEvents();
        await SeedPackageAsync(database, package, events);

        await using (var migrationContext = database.CreateContext())
        {
            await migrationContext.Database.MigrateAsync(CancellationToken.None);
            var applied = (await migrationContext.Database
                    .GetAppliedMigrationsAsync(CancellationToken.None))
                .ToArray();
            Assert.EndsWith(
                "_BackfillPendingPackageEvents",
                applied[^1],
                StringComparison.Ordinal);
        }

        await using var context = database.CreateContext();
        var stored = await context.Packages
            .AsNoTracking()
            .SingleAsync(
                candidate => candidate.Name == package.Key.Name.Value,
                CancellationToken.None);
        Assert.Equal("[]", stored.PendingEventsJson);

        var outboxRecords = await context.PackageOutbox
            .AsNoTracking()
            .OrderBy(record => record.OccurredAt)
            .ToListAsync(CancellationToken.None);

        Assert.Equal(events.Count, outboxRecords.Count);

        foreach (var domainEvent in events)
        {
            var expected = PostgresOutboxEventSerializer.ToOutboxEvent(domainEvent);
            var storedRecord = Assert.Single(
                outboxRecords,
                record => record.IdempotencyKey == expected.IdempotencyKey);

            Assert.Equal(expected.EventType, storedRecord.EventType);
            Assert.Equal(expected.PackageName, storedRecord.PackageName);
            Assert.Equal(expected.PackageVersion, storedRecord.PackageVersion);
            Assert.Equal(expected.Payload, storedRecord.Payload);
            Assert.Equal((int)PackageOutboxStatus.Pending, storedRecord.Status);
            Assert.Equal(0, storedRecord.AttemptCount);
            Assert.Null(storedRecord.ClaimedAt);
            Assert.Null(storedRecord.ClaimId);
            Assert.Null(storedRecord.LastError);

            var rehydrated = Assert.Single(
                PostgresOutboxEventSerializer.Deserialize(
                    storedRecord.EventType,
                    storedRecord.Payload));
            Assert.Equal(domainEvent, rehydrated);
        }
    }

    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task NewWrites_InsertOutboxRowsAndClearPendingEventsJson()
    {
        await using var database = await PostgresTestDatabase.CreateAsync(CancellationToken.None);
        await using (var migrationContext = database.CreateContext())
        {
            await migrationContext.Database.MigrateAsync(CancellationToken.None);
        }

        var repository = new PostgresPackageRepository(database.CreateFactory());
        var package = CreatePackageWithFractionalTimestamps();
        Assert.True(await repository.TryAddWithEventsAsync(
            package,
            package.PeekDomainEvents(),
            CancellationToken.None));

        var loaded = await repository.FindAsync(package.Key, CancellationToken.None);
        Assert.NotNull(loaded);
        Assert.Empty(loaded.PeekDomainEvents());
        loaded.RequestBuild(package.RegisteredAt.AddMinutes(1));
        await repository.UpdateWithEventsAsync(
            loaded,
            loaded.PeekDomainEvents(),
            CancellationToken.None);

        await using var context = database.CreateContext();
        var stored = await context.Packages
            .AsNoTracking()
            .SingleAsync(
                candidate => candidate.Name == package.Key.Name.Value,
                CancellationToken.None);
        Assert.Equal("[]", stored.PendingEventsJson);

        var outboxRecords = await context.PackageOutbox
            .AsNoTracking()
            .OrderBy(record => record.OccurredAt)
            .ToListAsync(CancellationToken.None);
        Assert.Equal(2, outboxRecords.Count);
        Assert.Equal(
            PostgresOutboxEventSerializer.RegisteredEventType,
            outboxRecords[0].EventType);
        Assert.Equal(
            PostgresOutboxEventSerializer.BuildRequestedEventType,
            outboxRecords[1].EventType);
    }

    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task EmptyEventCollection_LeavesPendingEventsJsonUnchanged()
    {
        await using var database = await PostgresTestDatabase.CreateAsync(CancellationToken.None);
        await using (var migrationContext = database.CreateContext())
        {
            await migrationContext.Database.MigrateAsync(CancellationToken.None);
        }

        var repository = new PostgresPackageRepository(database.CreateFactory());
        var package = CreatePackageWithFractionalTimestamps();
        var events = package.PeekDomainEvents();
        Assert.True(await repository.TryAddWithEventsAsync(
            package,
            events,
            CancellationToken.None));

        var serialized = PostgresPendingEventSerializer.Serialize(events);
        await SetPendingEventsJsonAsync(
            database,
            package.Key,
            serialized,
            CancellationToken.None);

        var loaded = await repository.FindAsync(package.Key, CancellationToken.None);
        Assert.NotNull(loaded);
        await repository.UpdateWithEventsAsync(
            loaded,
            Array.Empty<IDomainEvent>(),
            CancellationToken.None);

        await using var context = database.CreateContext();
        var stored = await context.Packages
            .AsNoTracking()
            .SingleAsync(
                candidate => candidate.Name == package.Key.Name.Value,
                CancellationToken.None);
        Assert.Equal(serialized, stored.PendingEventsJson);
        Assert.Equal(
            1,
            await context.PackageOutbox.CountAsync(CancellationToken.None));
    }

    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task Backfill_WaitsForConcurrentLegacyWriterAndProcessesCommittedDescriptors()
    {
        await using var database = await CreateDatabaseAtPreviousMigrationAsync();
        var package = CreatePackageWithFractionalTimestamps(
            name: $"lock-{Guid.NewGuid():N}");
        await SeedPackageAsync(database, package, package.PeekDomainEvents());

        var registeredEvent = Assert.Single(package.PeekDomainEvents());
        var buildRequestedAt = package.RegisteredAt.AddMinutes(1);
        var buildEvent = new PackageBuildRequested(
            package.Key,
            package.BuildPolicy,
            buildRequestedAt);
        var writerJson = PostgresPendingEventSerializer.Serialize(
            [registeredEvent, buildEvent]);

        await using var writerConnection = new NpgsqlConnection(database.ConnectionString);
        await writerConnection.OpenAsync(CancellationToken.None);
        await using var writerTransaction =
            await writerConnection.BeginTransactionAsync(CancellationToken.None);

        await using (var writerCommand = new NpgsqlCommand(
            """
            UPDATE packages
            SET "PendingEventsJson" = @json,
                "State" = 1,
                "BuildRequestedAt" = @buildRequestedAt
            WHERE "Name" = @name
              AND "Version" = @version
            """,
            writerConnection,
            writerTransaction))
        {
            writerCommand.Parameters.AddWithValue("json", writerJson);
            writerCommand.Parameters.AddWithValue("buildRequestedAt", buildRequestedAt);
            writerCommand.Parameters.AddWithValue("name", package.Key.Name.Value);
            writerCommand.Parameters.AddWithValue(
                "version",
                package.Key.Version.ToString());
            Assert.Equal(
                1,
                await writerCommand.ExecuteNonQueryAsync(CancellationToken.None));
        }

        await using var context = database.CreateContext();
        var backfillTask = context.Database.ExecuteSqlRawAsync(
            PostgresPendingEventBackfillSql.Sql,
            CancellationToken.None);

        await Task.Delay(TimeSpan.FromMilliseconds(250), CancellationToken.None);
        Assert.False(backfillTask.IsCompleted);

        await writerTransaction.CommitAsync(CancellationToken.None);
        await backfillTask.WaitAsync(TimeSpan.FromSeconds(30), CancellationToken.None);

        var stored = await context.Packages
            .AsNoTracking()
            .SingleAsync(
                candidate => candidate.Name == package.Key.Name.Value,
                CancellationToken.None);
        Assert.Equal("[]", stored.PendingEventsJson);
        Assert.Equal(
            2,
            await context.PackageOutbox.CountAsync(CancellationToken.None));
    }

    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task Backfill_IsSafeToRunTwice()
    {
        await using var database = await CreateDatabaseAtPreviousMigrationAsync();
        var package = CreatePackageWithFractionalTimestamps();
        var events = package.PeekDomainEvents();
        await SeedPackageAsync(database, package, events);

        await using var context = database.CreateContext();
        await context.Database.ExecuteSqlRawAsync(
            PostgresPendingEventBackfillSql.Sql,
            CancellationToken.None);
        await context.Database.ExecuteSqlRawAsync(
            PostgresPendingEventBackfillSql.Sql,
            CancellationToken.None);

        Assert.Equal(
            1,
            await context.PackageOutbox.CountAsync(CancellationToken.None));
        var stored = await context.Packages
            .AsNoTracking()
            .SingleAsync(
                candidate => candidate.Name == package.Key.Name.Value,
                CancellationToken.None);
        Assert.Equal("[]", stored.PendingEventsJson);
    }

    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task Backfill_DedupesExistingWriterIdempotencyKey()
    {
        await using var database = await CreateDatabaseAtPreviousMigrationAsync();
        var package = CreatePackageWithFractionalTimestamps();
        var domainEvent = Assert.Single(package.PeekDomainEvents());
        var expected = PostgresOutboxEventSerializer.ToOutboxEvent(domainEvent);
        await SeedPackageAsync(database, package, [domainEvent]);

        await using (var seedContext = database.CreateContext())
        {
            seedContext.PackageOutbox.Add(new PackageOutboxRecord
            {
                IdempotencyKey = expected.IdempotencyKey,
                PackageName = expected.PackageName,
                PackageVersion = expected.PackageVersion,
                EventType = expected.EventType,
                Payload = expected.Payload,
                OccurredAt = expected.OccurredAt,
                AvailableAt = expected.OccurredAt,
                AttemptCount = 0,
                Status = (int)PackageOutboxStatus.Pending
            });
            await seedContext.SaveChangesAsync(CancellationToken.None);
        }

        await using var context = database.CreateContext();
        await context.Database.ExecuteSqlRawAsync(
            PostgresPendingEventBackfillSql.Sql,
            CancellationToken.None);

        Assert.Equal(
            1,
            await context.PackageOutbox.CountAsync(CancellationToken.None));
        var stored = await context.Packages
            .AsNoTracking()
            .SingleAsync(
                candidate => candidate.Name == package.Key.Name.Value,
                CancellationToken.None);
        Assert.Equal("[]", stored.PendingEventsJson);
    }

    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task UnsupportedDescriptor_FailsClosedAndRollsBackEveryChange()
    {
        await using var database = await CreateDatabaseAtPreviousMigrationAsync();
        var validPackage = CreatePackageWithFractionalTimestamps(
            name: $"a-valid-{Guid.NewGuid():N}");
        var invalidPackage = CreatePackageWithFractionalTimestamps(
            name: $"z-invalid-{Guid.NewGuid():N}");
        var validJson = PostgresPendingEventSerializer.Serialize(
            validPackage.PeekDomainEvents());
        const string invalidJson =
            """[{"type":"PackageDeleted","name":"z-invalid","version":"1.2.3","occurredAt":"2026-01-02T03:04:05+00:00"}]""";

        await SeedPackageAsync(
            database,
            validPackage,
            validPackage.PeekDomainEvents());
        await SetPendingEventsJsonAsync(
            database,
            validPackage.Key,
            validJson,
            CancellationToken.None);
        await SeedPackageAsync(
            database,
            invalidPackage,
            invalidPackage.PeekDomainEvents());
        await SetPendingEventsJsonAsync(
            database,
            invalidPackage.Key,
            invalidJson,
            CancellationToken.None);

        await using var context = database.CreateContext();
        var exception = await Assert.ThrowsAsync<PostgresException>(
            () => context.Database.ExecuteSqlRawAsync(
                PostgresPendingEventBackfillSql.Sql,
                CancellationToken.None));

        Assert.Contains(
            "Unsupported pending package event type",
            exception.MessageText,
            StringComparison.Ordinal);
        Assert.Equal(
            0,
            await context.PackageOutbox.CountAsync(CancellationToken.None));

        await using var readContext = database.CreateContext();
        var validStored = await readContext.Packages
            .AsNoTracking()
            .SingleAsync(
                candidate => candidate.Name == validPackage.Key.Name.Value,
                CancellationToken.None);
        var invalidStored = await readContext.Packages
            .AsNoTracking()
            .SingleAsync(
                candidate => candidate.Name == invalidPackage.Key.Name.Value,
                CancellationToken.None);
        Assert.Equal(validJson, validStored.PendingEventsJson);
        Assert.Equal(invalidJson, invalidStored.PendingEventsJson);
    }

    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task MalformedDescriptor_FailsClosedAndPreservesOriginalJson()
    {
        await using var database = await CreateDatabaseAtPreviousMigrationAsync();
        var package = CreatePackageWithFractionalTimestamps();
        const string malformedJson = """[{"type":"PackageRegistered",""";
        await SeedPackageAsync(
            database,
            package,
            package.PeekDomainEvents());
        await SetPendingEventsJsonAsync(
            database,
            package.Key,
            malformedJson,
            CancellationToken.None);

        await using var context = database.CreateContext();
        await Assert.ThrowsAsync<PostgresException>(
            () => context.Database.ExecuteSqlRawAsync(
                PostgresPendingEventBackfillSql.Sql,
                CancellationToken.None));

        Assert.Equal(
            0,
            await context.PackageOutbox.CountAsync(CancellationToken.None));
        var stored = await context.Packages
            .AsNoTracking()
            .SingleAsync(
                candidate => candidate.Name == package.Key.Name.Value,
                CancellationToken.None);
        Assert.Equal(malformedJson, stored.PendingEventsJson);
    }

    private static async Task<PostgresTestDatabase> CreateDatabaseAtPreviousMigrationAsync()
    {
        var database = await PostgresTestDatabase.CreateAsync(CancellationToken.None);
        await using var context = database.CreateContext();
        await context.Database.MigrateAsync(
            PreviousMigration,
            CancellationToken.None);
        return database;
    }

    private static Package CreatePackageWithFractionalTimestamps(string? name = null) =>
        Package.Register(
            TestPackages.CreateManifest(name: name ?? $"backfill-{Guid.NewGuid():N}"),
            BuildPolicy.Create(
                BuildIsolation.Namespace,
                allowNetwork: false,
                timeout: TimeSpan.FromMinutes(5)),
            TestPackages.Timestamp.AddTicks(1_234_567));

    private static async Task SeedPackageAsync(
        PostgresTestDatabase database,
        Package package,
        IReadOnlyCollection<IDomainEvent> events)
    {
        var record = PostgresPackageMapper.FromDomain(package);
        record.PendingEventsJson = PostgresPendingEventSerializer.Serialize(events);

        await using var context = database.CreateContext();
        context.Packages.Add(record);
        await context.SaveChangesAsync(CancellationToken.None);
    }

    private static async Task SetPendingEventsJsonAsync(
        PostgresTestDatabase database,
        PackageKey key,
        string json,
        CancellationToken cancellationToken)
    {
        await using var context = database.CreateContext();
        await context.Packages
            .Where(record => record.Name == key.Name.Value
                && record.Version == key.Version.ToString())
            .ExecuteUpdateAsync(
                setters => setters.SetProperty(
                    record => record.PendingEventsJson,
                    json),
                cancellationToken);
    }
}
