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
    public async Task Search_MatchesNameContainsVersionContainsAndSourceCommitPrefix()
    {
        var repository = new InMemoryPackageRepository();
        await repository.TryAddAsync(
            TestPackages.CreatePackage(name: "libc", version: "2.3.4-rc.1+build.7"),
            CancellationToken.None);

        var nameContains = await repository.SearchAsync("IBC", CancellationToken.None);
        var versionContains = await repository.SearchAsync("RC.1", CancellationToken.None);
        var buildMetadataContains = await repository.SearchAsync("build.7", CancellationToken.None);
        var commitPrefix = await repository.SearchAsync(
            TestPackages.CommitSha[..8],
            CancellationToken.None);
        var commitSubstring = await repository.SearchAsync(
            TestPackages.CommitSha[8..16],
            CancellationToken.None);
        var provenanceOnly = await repository.SearchAsync(
            "ci://pkgsrv/tests/42",
            CancellationToken.None);

        Assert.Equal("libc", Assert.Single(nameContains).Manifest.Name.Value);
        Assert.Equal("libc", Assert.Single(versionContains).Manifest.Name.Value);
        Assert.Equal("libc", Assert.Single(buildMetadataContains).Manifest.Name.Value);
        Assert.Equal("libc", Assert.Single(commitPrefix).Manifest.Name.Value);
        Assert.Empty(commitSubstring);
        Assert.Empty(provenanceOnly);
    }

    [Fact]
    public async Task Search_TreatsWildcardCharactersLiterally()
    {
        var repository = new InMemoryPackageRepository();
        await repository.TryAddAsync(TestPackages.CreatePackage(name: "a_b"), CancellationToken.None);
        await repository.TryAddAsync(TestPackages.CreatePackage(name: "ab"), CancellationToken.None);

        var underscore = await repository.SearchAsync("_", CancellationToken.None);
        var percent = await repository.SearchAsync("%", CancellationToken.None);
        var literal = await repository.SearchAsync("a_b", CancellationToken.None);

        Assert.Equal("a_b", Assert.Single(underscore).Manifest.Name.Value);
        Assert.Empty(percent);
        Assert.Equal("a_b", Assert.Single(literal).Manifest.Name.Value);
    }

    [Fact]
    public async Task Search_OrdersNumericVersionComponentsAndReleasesAfterPreReleases()
    {
        var repository = new InMemoryPackageRepository();
        await repository.TryAddAsync(TestPackages.CreatePackage(version: "1.10.0"), CancellationToken.None);
        await repository.TryAddAsync(TestPackages.CreatePackage(version: "1.9.0"), CancellationToken.None);
        await repository.TryAddAsync(TestPackages.CreatePackage(version: "1.9.1-rc.10"), CancellationToken.None);
        await repository.TryAddAsync(TestPackages.CreatePackage(version: "1.9.1-rc.2"), CancellationToken.None);
        await repository.TryAddAsync(TestPackages.CreatePackage(version: "1.9.1"), CancellationToken.None);

        var all = await repository.SearchAsync(null, CancellationToken.None);

        Assert.Equal(
            ["1.9.0", "1.9.1-rc.2", "1.9.1-rc.10", "1.9.1", "1.10.0"],
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
