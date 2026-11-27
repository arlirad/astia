using Microsoft.EntityFrameworkCore;
using Pkgsrv.Infrastructure.Packages;
using Pkgsrv.Infrastructure.Persistence.Postgres;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPackageOrderingIntegrationTests
{
    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task Search_OrdersWithTheSameSemVerPrecedenceAsInMemory()
    {
        await using var database = await PostgresTestDatabase.CreateAsync(CancellationToken.None);
        await using (var context = database.CreateContext())
        {
            await context.Database.MigrateAsync(CancellationToken.None);
        }

        var name = $"order-{Guid.NewGuid():N}";
        string[] versions =
        [
            "1.0.0",
            "1.0.0-rc.10",
            "1.0.0-rc.2",
            "1.0.0-beta.11",
            "1.0.0-beta.2",
            "1.0.0-beta",
            "1.0.0-alpha.beta",
            "1.0.0-alpha.1",
            "1.0.0-alpha",
            "1.0.0-1",
            "1.0.0-2",
            "1.0.0+build.10",
            "1.0.0+build.2"
        ];

        var postgres = new PostgresPackageRepository(database.CreateFactory());
        var inMemory = new InMemoryPackageRepository();

        foreach (var version in versions)
        {
            var package = TestPackages.CreatePackage(name: name, version: version);

            Assert.True(await postgres.TryAddAsync(package, CancellationToken.None));
            Assert.True(await inMemory.TryAddAsync(package, CancellationToken.None));
        }

        var postgresOrder = (await postgres.SearchAsync(null, CancellationToken.None))
            .Select(entry => entry.Manifest.Version.ToString())
            .ToArray();
        var inMemoryOrder = (await inMemory.SearchAsync(null, CancellationToken.None))
            .Select(entry => entry.Manifest.Version.ToString())
            .ToArray();

        Assert.Equal(inMemoryOrder, postgresOrder);
        Assert.Equal(
            [
                "1.0.0-1",
                "1.0.0-2",
                "1.0.0-alpha",
                "1.0.0-alpha.1",
                "1.0.0-alpha.beta",
                "1.0.0-beta",
                "1.0.0-beta.2",
                "1.0.0-beta.11",
                "1.0.0-rc.2",
                "1.0.0-rc.10",
                "1.0.0",
                "1.0.0+build.10",
                "1.0.0+build.2"
            ],
            inMemoryOrder);
    }

    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task Search_MatchesVersionText()
    {
        await using var database = await PostgresTestDatabase.CreateAsync(CancellationToken.None);
        await using (var context = database.CreateContext())
        {
            await context.Database.MigrateAsync(CancellationToken.None);
        }

        var name = $"search-{Guid.NewGuid():N}";
        var repository = new PostgresPackageRepository(database.CreateFactory());
        var package = TestPackages.CreatePackage(
            name: name,
            version: "2.3.4-rc.1+build.7");

        Assert.True(await repository.TryAddAsync(package, CancellationToken.None));

        var byVersion = await repository.SearchAsync("2.3.4", CancellationToken.None);
        var byPreRelease = await repository.SearchAsync("rc.1", CancellationToken.None);
        var byMetadata = await repository.SearchAsync("BUILD.7", CancellationToken.None);
        var byMissing = await repository.SearchAsync("2.3.5", CancellationToken.None);

        Assert.Contains(byVersion, entry => entry.Manifest.Name.Value == name);
        Assert.Contains(byPreRelease, entry => entry.Manifest.Name.Value == name);
        Assert.Contains(byMetadata, entry => entry.Manifest.Name.Value == name);
        Assert.DoesNotContain(byMissing, entry => entry.Manifest.Name.Value == name);
    }
}
