using Microsoft.EntityFrameworkCore;
using Pkgsrv.Infrastructure.Persistence.Postgres;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPackageQueryTests
{
    [Fact]
    public void BuildSearchQuery_PushesFilterOrderingAndLimitIntoSql()
    {
        using var context = CreateContext();

        var sql = PostgresPackageRepository
            .BuildSearchQuery(context.Packages.AsNoTracking(), "50%_off")
            .ToQueryString();

        Assert.Contains("ILIKE", sql, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("ESCAPE", sql, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("ORDER BY", sql, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("LIMIT", sql, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("VersionMajor", sql, StringComparison.Ordinal);
    }

    [Fact]
    public void BuildSearchQuery_UnfilteredStillOrdersAndLimitsInSql()
    {
        using var context = CreateContext();

        var sql = PostgresPackageRepository
            .BuildSearchQuery(context.Packages.AsNoTracking(), null)
            .ToQueryString();

        Assert.DoesNotContain("ILIKE", sql, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("ORDER BY", sql, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("LIMIT", sql, StringComparison.OrdinalIgnoreCase);
    }

    private static PostgresPackageDbContext CreateContext()
    {
        var options = new DbContextOptionsBuilder<PostgresPackageDbContext>()
            .UseNpgsql("Host=localhost;Database=pkgsrv;Username=test;Password=test")
            .Options;

        return new PostgresPackageDbContext(options);
    }
}
