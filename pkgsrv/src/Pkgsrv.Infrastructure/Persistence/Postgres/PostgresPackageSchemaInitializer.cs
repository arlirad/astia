using Microsoft.EntityFrameworkCore;

namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public interface IPackageSchemaInitializer
{
    Task InitializeAsync(CancellationToken cancellationToken);
}

public sealed class PostgresPackageSchemaInitializer : IPackageSchemaInitializer
{
    private readonly IDbContextFactory<PostgresPackageDbContext> _contextFactory;

    public PostgresPackageSchemaInitializer(
        IDbContextFactory<PostgresPackageDbContext> contextFactory)
    {
        _contextFactory = contextFactory;
    }

    public async Task InitializeAsync(CancellationToken cancellationToken)
    {
        await using var context = await _contextFactory
            .CreateDbContextAsync(cancellationToken)
            .ConfigureAwait(false);

        await context.Database
            .ExecuteSqlRawAsync(
                PostgresPackageSchema.CreateSql,
                Array.Empty<object>(),
                cancellationToken)
            .ConfigureAwait(false);
    }
}
