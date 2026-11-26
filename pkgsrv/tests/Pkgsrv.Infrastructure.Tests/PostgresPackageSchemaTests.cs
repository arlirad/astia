using Microsoft.EntityFrameworkCore;
using Pkgsrv.Infrastructure.Persistence.Postgres;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPackageSchemaTests
{
    [Fact]
    public void SchemaSql_IsIdempotentAndNonDestructive()
    {
        var sql = PostgresPackageSchema.CreateSql;

        Assert.Contains("CREATE TABLE IF NOT EXISTS packages", sql, StringComparison.Ordinal);
        Assert.Contains("PRIMARY KEY (\"Name\", \"Version\")", sql, StringComparison.Ordinal);
        Assert.Contains("CREATE INDEX IF NOT EXISTS", sql, StringComparison.Ordinal);
        Assert.Contains("\"PendingEventsJson\" text NOT NULL", sql, StringComparison.Ordinal);
        Assert.Contains("ADD COLUMN IF NOT EXISTS \"PendingEventsJson\"", sql, StringComparison.Ordinal);
        Assert.Contains("\"TimeoutTicks\" bigint NOT NULL", sql, StringComparison.Ordinal);
        Assert.Contains("\"VersionMajor\" integer NOT NULL", sql, StringComparison.Ordinal);
        Assert.Contains("IX_packages_VersionOrder", sql, StringComparison.Ordinal);
        Assert.Contains("\"PublishedAt\" timestamp with time zone", sql, StringComparison.Ordinal);
        Assert.Contains("ADD COLUMN IF NOT EXISTS \"PublishedAt\"", sql, StringComparison.Ordinal);
        Assert.Contains("schema_versions", sql, StringComparison.Ordinal);
        Assert.Contains("VALUES (1), (2), (3)", sql, StringComparison.Ordinal);
        Assert.DoesNotContain("DROP TABLE", sql, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("TRUNCATE", sql, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void EntityModel_MapsPackageRecordToExpectedTableAndColumns()
    {
        var options = new DbContextOptionsBuilder<PostgresPackageDbContext>()
            .UseNpgsql("Host=localhost;Database=pkgsrv;Username=test;Password=test")
            .Options;
        using var context = new PostgresPackageDbContext(options);
        var entity = context.Model.FindEntityType(typeof(PackageRecord));

        Assert.NotNull(entity);
        Assert.Equal("packages", entity.GetTableName());

        var primaryKey = entity.FindPrimaryKey();
        Assert.NotNull(primaryKey);
        Assert.Equal(["Name", "Version"], primaryKey.Properties.Select(property => property.Name));

        Assert.Equal("timestamp with time zone", entity.FindProperty(nameof(PackageRecord.RegisteredAt))!.GetColumnType());
        Assert.Equal("timestamp with time zone", entity.FindProperty(nameof(PackageRecord.BuildRequestedAt))!.GetColumnType());
        Assert.NotNull(entity.FindProperty(nameof(PackageRecord.PendingEventsJson)));
        Assert.NotNull(entity.FindProperty(nameof(PackageRecord.TimeoutTicks)));
        Assert.NotNull(entity.FindProperty(nameof(PackageRecord.PublishedAt)));
    }

    [Fact]
    public void MigrationScripts_RecordVersionsInOrder()
    {
        var versionOne = File.ReadAllText(FindMigrationScript("0001_packages.sql"));
        var versionTwo = File.ReadAllText(FindMigrationScript("0002_packages_version_order.sql"));
        var versionThree = File.ReadAllText(FindMigrationScript("0003_packages_publication.sql"));

        Assert.Equal(3, PostgresPackageSchema.RequiredVersion);
        Assert.Contains("CREATE TABLE IF NOT EXISTS schema_versions", versionOne, StringComparison.Ordinal);
        Assert.Contains("\"TimeoutTicks\" bigint NOT NULL", versionOne, StringComparison.Ordinal);
        Assert.Contains("VALUES (1)", versionOne, StringComparison.Ordinal);
        Assert.DoesNotContain("\"VersionMajor\"", versionOne, StringComparison.Ordinal);

        Assert.Contains("\"VersionMajor\" integer", versionTwo, StringComparison.Ordinal);
        Assert.Contains("IX_packages_VersionOrder", versionTwo, StringComparison.Ordinal);
        Assert.Contains("VALUES (2)", versionTwo, StringComparison.Ordinal);

        Assert.Contains("\"PublishedAt\" timestamp with time zone", versionThree, StringComparison.Ordinal);
        Assert.Contains("ADD COLUMN IF NOT EXISTS \"PublishedAt\"", versionThree, StringComparison.Ordinal);
        Assert.Contains("VALUES (3)", versionThree, StringComparison.Ordinal);

        Assert.DoesNotContain("DROP TABLE", versionOne, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("DROP TABLE", versionTwo, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("DROP TABLE", versionThree, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("TRUNCATE", versionOne, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("TRUNCATE", versionTwo, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("TRUNCATE", versionThree, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void RequiredShape_ListsVersionOrderingColumns()
    {
        Assert.Contains("VersionMajor", PostgresPackageSchema.RequiredColumns);
        Assert.Contains("VersionPreRelease", PostgresPackageSchema.RequiredColumns);
        Assert.Contains("VersionBuildMetadata", PostgresPackageSchema.RequiredColumns);
        Assert.Contains("TimeoutTicks", PostgresPackageSchema.RequiredColumns);
        Assert.Contains("PublishedAt", PostgresPackageSchema.RequiredColumns);
    }

    private static string FindMigrationScript(string fileName)
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);

        while (directory is not null)
        {
            var candidate = Path.Combine(directory.FullName, "db", "migrations", fileName);

            if (File.Exists(candidate))
            {
                return candidate;
            }

            directory = directory.Parent;
        }

        throw new FileNotFoundException($"Could not locate pkgsrv/db/migrations/{fileName}.");
    }
}
