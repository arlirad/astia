using Microsoft.EntityFrameworkCore;
using Pkgsrv.Application.Errors;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Persistence.Postgres;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresRepositoryIntegrationTests
{
    [Fact]
    [Trait("Category", "Postgres")]
    public async Task PostgresRepository_OptionalIntegration_RunsWhenConnectionIsConfigured()
    {
        var connectionString = Environment.GetEnvironmentVariable("PKGSRV_TEST_POSTGRES_CONNECTION");

        if (string.IsNullOrWhiteSpace(connectionString))
        {
            return;
        }

        var options = new DbContextOptionsBuilder<PostgresPackageDbContext>()
            .UseNpgsql(connectionString)
            .Options;
        var factory = new TestDbContextFactory(options);
        await new PostgresPackageSchemaInitializer(factory).InitializeAsync(CancellationToken.None);
        var repository = new PostgresPackageRepository(factory);
        var name = $"it-{Guid.NewGuid():N}";
        var package = TestPackages.CreatePackage(name: name);

        try
        {
            Assert.True(await repository.TryAddAsync(package, CancellationToken.None));

            var found = await repository.FindAsync(package.Key, CancellationToken.None);
            Assert.NotNull(found);
            Assert.Equal(package.Manifest.ContentHash, found.Manifest.ContentHash);
            var pendingRegistration = found.PeekDomainEvents();
            Assert.IsType<PackageRegistered>(Assert.Single(pendingRegistration));

            await repository.AcknowledgeDomainEventsAsync(
                found.Key,
                pendingRegistration,
                CancellationToken.None);
            found.AcknowledgeDomainEvents(pendingRegistration);

            var afterAcknowledgement = await repository.FindAsync(found.Key, CancellationToken.None);
            Assert.NotNull(afterAcknowledgement);
            Assert.Empty(afterAcknowledgement.PeekDomainEvents());

            found.RequestBuild(TestPackages.Timestamp.AddMinutes(1));
            await repository.UpdateAsync(found, CancellationToken.None);

            var updated = await repository.FindAsync(package.Key, CancellationToken.None);
            Assert.NotNull(updated);
            Assert.Equal(PackageState.BuildRequested, updated.State);
            Assert.Equal(TestPackages.Timestamp.AddMinutes(1), updated.BuildRequestedAt);

            var stale = TestPackages.CreatePackage(name: name);
            await Assert.ThrowsAsync<PackageConflictException>(
                () => repository.UpdateAsync(stale, CancellationToken.None));

            var results = await repository.SearchAsync(name, CancellationToken.None);
            Assert.Contains(results, entry => entry.Manifest.Name.Value == name);
        }
        finally
        {
            await using var context = factory.CreateDbContext();
            await context.Database.ExecuteSqlRawAsync(
                "DELETE FROM packages WHERE \"Name\" = {0}",
                name);
        }
    }

    private sealed class TestDbContextFactory : IDbContextFactory<PostgresPackageDbContext>
    {
        private readonly DbContextOptions<PostgresPackageDbContext> _options;

        public TestDbContextFactory(DbContextOptions<PostgresPackageDbContext> options)
        {
            _options = options;
        }

        public PostgresPackageDbContext CreateDbContext() => new(_options);
    }
}
