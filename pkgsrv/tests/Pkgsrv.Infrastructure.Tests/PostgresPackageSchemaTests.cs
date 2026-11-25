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
        Assert.DoesNotContain("DROP", sql, StringComparison.OrdinalIgnoreCase);
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
    }
}
