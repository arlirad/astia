using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Packages;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class InMemoryPackageRepositoryTests
{
    [Fact]
    public async Task AddAndFind_RoundTripsPackage()
    {
        var repository = new InMemoryPackageRepository();
        var package = TestPackages.CreatePackage();

        Assert.True(await repository.TryAddAsync(package, CancellationToken.None));

        var found = await repository.FindAsync(package.Key, CancellationToken.None);

        Assert.Same(package, found);
    }

    [Fact]
    public async Task TryAdd_RejectsDuplicatePackageKey()
    {
        var repository = new InMemoryPackageRepository();
        await repository.TryAddAsync(TestPackages.CreatePackage(), CancellationToken.None);

        Assert.False(await repository.TryAddAsync(
            TestPackages.CreatePackage(),
            CancellationToken.None));
    }

    [Fact]
    public async Task Search_FiltersAndOrdersDeterministically()
    {
        var repository = new InMemoryPackageRepository();
        await repository.TryAddAsync(TestPackages.CreatePackage(name: "widget"), CancellationToken.None);
        await repository.TryAddAsync(TestPackages.CreatePackage(name: "libc"), CancellationToken.None);

        var all = await repository.SearchAsync(null, CancellationToken.None);
        var filtered = await repository.SearchAsync("lib", CancellationToken.None);

        Assert.Equal(["libc", "widget"], all.Select(entry => entry.Manifest.Name.Value));
        Assert.Equal("libc", Assert.Single(filtered).Manifest.Name.Value);
    }

    [Fact]
    public async Task Search_OrdersNumericVersionComponentsAndReleasesAfterPreReleases()
    {
        var repository = new InMemoryPackageRepository();
        await repository.TryAddAsync(TestPackages.CreatePackage(version: "1.10.0"), CancellationToken.None);
        await repository.TryAddAsync(TestPackages.CreatePackage(version: "1.9.0"), CancellationToken.None);
        await repository.TryAddAsync(TestPackages.CreatePackage(version: "1.9.1-rc.1"), CancellationToken.None);
        await repository.TryAddAsync(TestPackages.CreatePackage(version: "1.9.1"), CancellationToken.None);

        var all = await repository.SearchAsync(null, CancellationToken.None);

        Assert.Equal(
            ["1.9.0", "1.9.1-rc.1", "1.9.1", "1.10.0"],
            all.Select(entry => entry.Manifest.Version.ToString()));
    }

    [Fact]
    public async Task Update_PersistsAggregateState()
    {
        var repository = new InMemoryPackageRepository();
        var package = TestPackages.CreatePackage();
        await repository.TryAddAsync(package, CancellationToken.None);

        package.RequestBuild(TestPackages.Timestamp);
        await repository.UpdateAsync(package, CancellationToken.None);

        var found = await repository.FindAsync(package.Key, CancellationToken.None);
        Assert.Equal(PackageState.BuildRequested, found!.State);
    }

    [Fact]
    public async Task Update_RejectsUnknownPackage()
    {
        var repository = new InMemoryPackageRepository();

        await Assert.ThrowsAsync<InvalidOperationException>(
            async () => await repository.UpdateAsync(
                TestPackages.CreatePackage(),
                CancellationToken.None));
    }
}
