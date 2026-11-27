using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Infrastructure;
using Microsoft.EntityFrameworkCore.Migrations;
using Pkgsrv.Infrastructure.Persistence.Postgres;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPackageModelTests
{
    private static readonly DbContextOptions<PostgresPackageDbContext> Options =
        new DbContextOptionsBuilder<PostgresPackageDbContext>()
            .UseNpgsql("Host=localhost;Database=pkgsrv;Username=test;Password=test")
            .Options;

    [Fact]
    public void Model_MatchesEfMigrationSnapshot()
    {
        using var context = new PostgresPackageDbContext(Options);

        Assert.False(context.Database.HasPendingModelChanges());
    }

    [Fact]
    public void Model_MapsPackageRecordToCurrentShape()
    {
        using var context = new PostgresPackageDbContext(Options);
        var entity = context.Model.FindEntityType(typeof(PackageRecord));

        Assert.NotNull(entity);
        Assert.Equal("packages", entity.GetTableName());
        Assert.Equal(
            ["Name", "Version"],
            entity.FindPrimaryKey()!.Properties.Select(property => property.Name));

        Assert.NotNull(entity.FindProperty(nameof(PackageRecord.TimeoutTicks)));
        Assert.NotNull(entity.FindProperty(nameof(PackageRecord.VersionMajor)));
        Assert.NotNull(entity.FindProperty(nameof(PackageRecord.VersionMinor)));
        Assert.NotNull(entity.FindProperty(nameof(PackageRecord.VersionPatch)));
        Assert.NotNull(entity.FindProperty(nameof(PackageRecord.VersionPreRelease)));
        Assert.NotNull(entity.FindProperty(nameof(PackageRecord.VersionBuildMetadata)));
        Assert.NotNull(entity.FindProperty(nameof(PackageRecord.VersionPreReleaseSortKey)));
        Assert.NotNull(entity.FindProperty(nameof(PackageRecord.PublishedAt)));
        Assert.Contains(
            entity.GetIndexes(),
            index => index.GetDatabaseName() == "IX_packages_VersionOrder");
    }

    [Fact]
    public void MigrationChain_IsOrderedAndOwnsEfHistory()
    {
        using var context = new PostgresPackageDbContext(Options);
        var migrations = context.Database.GetMigrations().ToArray();

        Assert.Equal(6, migrations.Length);
        Assert.EndsWith("_InitialCreate", migrations[0], StringComparison.Ordinal);
        Assert.EndsWith("_AddVersionOrderColumns", migrations[1], StringComparison.Ordinal);
        Assert.EndsWith("_AddPublicationTimestamp", migrations[2], StringComparison.Ordinal);
        Assert.EndsWith("_AddPackageOutbox", migrations[3], StringComparison.Ordinal);
        Assert.EndsWith("_AddSemVerPrecedenceKey", migrations[4], StringComparison.Ordinal);
        Assert.EndsWith("_BackfillPendingPackageEvents", migrations[5], StringComparison.Ordinal);

        var script = context.GetService<IMigrator>().GenerateScript();

        Assert.Contains("__EFMigrationsHistory", script, StringComparison.Ordinal);
        Assert.Contains("CREATE TABLE IF NOT EXISTS packages", script, StringComparison.Ordinal);
        Assert.Contains("\"TimeoutTicks\" bigint", script, StringComparison.Ordinal);
        Assert.Contains("\"VersionMajor\" integer", script, StringComparison.Ordinal);
        Assert.Contains("\"VersionPreReleaseSortKey\" text", script, StringComparison.Ordinal);
        Assert.Contains("IX_packages_VersionOrder", script, StringComparison.Ordinal);
        Assert.Contains("\"PublishedAt\" timestamp with time zone", script, StringComparison.Ordinal);
        Assert.Contains("package_outbox", script, StringComparison.Ordinal);
        Assert.Contains("jsonb_array_elements", script, StringComparison.Ordinal);
        Assert.Contains("WITH ORDINALITY", script, StringComparison.Ordinal);
        Assert.Contains(
            "ON CONFLICT (\"IdempotencyKey\") DO NOTHING",
            script,
            StringComparison.Ordinal);
        Assert.Contains(
            "SET \"PendingEventsJson\" = '[]'",
            script,
            StringComparison.Ordinal);
    }
}
