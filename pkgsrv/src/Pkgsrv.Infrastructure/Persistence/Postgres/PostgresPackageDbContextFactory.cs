using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Design;

namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public sealed class PostgresPackageDbContextFactory
    : IDesignTimeDbContextFactory<PostgresPackageDbContext>
{
    public PostgresPackageDbContext CreateDbContext(string[] args)
    {
        var connectionString = Environment.GetEnvironmentVariable(
            "PKGSRV_DESIGN_POSTGRES_CONNECTION")
            ?? "Host=localhost;Database=pkgsrv";

        var options = new DbContextOptionsBuilder<PostgresPackageDbContext>()
            .UseNpgsql(connectionString)
            .Options;

        return new PostgresPackageDbContext(options);
    }
}
