using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Persistence.Postgres;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPendingEventBackfillSqlTests
{
    [Fact]
    public void Sql_ExpandsLegacyDescriptorsAndClearsAfterInsert()
    {
        var sql = PostgresPendingEventBackfillSql.Sql;

        Assert.Contains("jsonb_array_elements", sql, StringComparison.Ordinal);
        Assert.Contains("WITH ORDINALITY", sql, StringComparison.Ordinal);
        Assert.Contains("ON CONFLICT (\"IdempotencyKey\") DO NOTHING", sql, StringComparison.Ordinal);
        Assert.Contains("INSERT INTO package_outbox", sql, StringComparison.Ordinal);
        Assert.Contains("SET \"PendingEventsJson\" = '[]'", sql, StringComparison.Ordinal);
        Assert.True(
            sql.IndexOf("INSERT INTO package_outbox", StringComparison.Ordinal)
            < sql.IndexOf("SET \"PendingEventsJson\" = '[]'", StringComparison.Ordinal));

        var lockIndex = sql.IndexOf(
            "LOCK TABLE packages IN ACCESS EXCLUSIVE MODE",
            StringComparison.Ordinal);
        var scanIndex = sql.IndexOf("jsonb_array_elements", StringComparison.Ordinal);
        Assert.True(lockIndex >= 0);
        Assert.True(scanIndex > lockIndex);

        Assert.Contains(
            "CREATE TEMP TABLE pkgsrv_processed_packages",
            sql,
            StringComparison.Ordinal);
        Assert.Contains(
            "INSERT INTO pkgsrv_processed_packages",
            sql,
            StringComparison.Ordinal);

        var updateIndex = sql.IndexOf(
            "UPDATE packages AS package_row",
            StringComparison.Ordinal);
        Assert.True(updateIndex > 0);
        var updateSql = sql[updateIndex..];
        Assert.Contains(
            "FROM pkgsrv_processed_packages AS processed",
            updateSql,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            "btrim(\"PendingEventsJson\")",
            updateSql,
            StringComparison.Ordinal);
        Assert.Contains(
            "\"signatureStatus\":",
            sql,
            StringComparison.Ordinal);
        Assert.Contains(
            "\"buildIsolation\":",
            sql,
            StringComparison.Ordinal);
        Assert.Contains(
            "\"timeoutTicks\":",
            sql,
            StringComparison.Ordinal);
        Assert.Contains(
            "has malformed PendingEventsJson",
            sql,
            StringComparison.Ordinal);
        Assert.Contains(
            "PendingEventsJson is not a JSON array",
            sql,
            StringComparison.Ordinal);
        Assert.Contains(
            "Unsupported pending package event type",
            sql,
            StringComparison.Ordinal);
        Assert.Contains(
            "does not match package row",
            sql,
            StringComparison.Ordinal);
        Assert.Contains(
            "is not an ISO-8601 timestamp",
            sql,
            StringComparison.Ordinal);
        Assert.Contains(
            "signed status without signature fields",
            sql,
            StringComparison.Ordinal);
        Assert.Contains(
            "build event descriptor without a persisted build request timestamp",
            sql,
            StringComparison.Ordinal);
    }

    [Fact]
    public void DescriptorWith100NanosecondPrecision_PreservesWriterIdempotencyKey()
    {
        var occurredAt = TestPackages.Timestamp.AddTicks(1_234_567);
        var package = Package.Register(
            TestPackages.CreateManifest(name: "nanosecond"),
            BuildPolicy.Create(
                BuildIsolation.Namespace,
                allowNetwork: false,
                timeout: TimeSpan.FromMinutes(5)),
            occurredAt);
        var original = Assert.Single(package.PeekDomainEvents());
        var json = PostgresPendingEventSerializer.Serialize([original]);

        var rehydrated = Assert.Single(
            PostgresPendingEventSerializer.Deserialize(
                json,
                package.Manifest,
                package.BuildPolicy));

        Assert.Equal(original, rehydrated);
        Assert.Equal(
            PostgresOutboxEventSerializer.ToOutboxEvent(original).IdempotencyKey,
            PostgresOutboxEventSerializer.ToOutboxEvent(rehydrated).IdempotencyKey);
    }

    [Fact]
    public void Sql_IsCompileTimeStaticConstant()
    {
        var field = typeof(PostgresPendingEventBackfillSql).GetField(
            nameof(PostgresPendingEventBackfillSql.Sql),
            System.Reflection.BindingFlags.Public
                | System.Reflection.BindingFlags.Static);

        Assert.NotNull(field);
        Assert.True(field.IsLiteral);
    }
}
