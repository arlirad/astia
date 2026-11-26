using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using Pkgsrv.Application.Interfaces.Caching;
using Pkgsrv.Infrastructure.Caching.Postgres;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPackageCacheInvalidationIntegrationTests
{
    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task PublisherAndListener_DeliverInvalidationPayloads()
    {
        await using var database = await PostgresTestDatabase.CreateAsync(
            CancellationToken.None);
        await using var dataSource = NpgsqlDataSource.Create(database.ConnectionString);
        var invalidator = new RecordingInvalidator();
        var listener = new PostgresPackageCacheInvalidationListener(
            dataSource,
            invalidator,
            NullLogger<PostgresPackageCacheInvalidationListener>.Instance);
        var publisher = new PostgresPackageCacheInvalidationPublisher(dataSource);

        await listener.StartAsync(CancellationToken.None);

        try
        {
            var expected = PackageCacheInvalidation.ForPackage(
                Pkgsrv.Domain.Packages.PackageKey.Create("widget", "1.2.3"));
            var deadline = DateTime.UtcNow.AddSeconds(15);

            while (invalidator.Invalidations.Count == 0
                && DateTime.UtcNow < deadline)
            {
                await publisher.PublishAsync(expected, CancellationToken.None);
                await Task.Delay(200);
            }

            Assert.Contains(
                invalidator.Invalidations,
                invalidation => invalidation.Scope == expected.Scope
                    && invalidation.Package == expected.Package);
        }
        finally
        {
            await listener.StopAsync(CancellationToken.None);
        }
    }

    private sealed class RecordingInvalidator : IPackageCacheInvalidator
    {
        public List<PackageCacheInvalidation> Invalidations { get; } = new();

        public void Invalidate(PackageCacheInvalidation invalidation) =>
            Invalidations.Add(invalidation);
    }
}
