using Microsoft.EntityFrameworkCore;
using Pkgsrv.Application.Errors;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Persistence.Postgres;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresRepositoryIntegrationTests
{
    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task PostgresRepository_UsesMigratedSchemaForCrudAndStateTransitions()
    {
        await using var database = await PostgresTestDatabase.CreateAsync(CancellationToken.None);
        await using (var context = database.CreateContext())
        {
            await context.Database.MigrateAsync(CancellationToken.None);
        }

        var factory = database.CreateFactory();
        var repository = new PostgresPackageRepository(factory);
        var name = $"it-{Guid.NewGuid():N}";
        var package = TestPackages.CreatePackage(name: name);

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
}
