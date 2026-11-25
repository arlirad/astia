using Pkgsrv.Application.Abstractions.Persistence;
using Pkgsrv.Application.Caching;
using Pkgsrv.Application.Contracts;
using Pkgsrv.Domain.Packages;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Application.Tests;

public sealed class JsonPackageReadModelCacheTests
{
    [Fact]
    public void GetDetails_ReturnsNullBeforeSetThenRoundTripsSerializedDetails()
    {
        var clock = new TestClock();
        var cache = CreateCache(clock, maxEntries: 8);
        var package = TestPackages.CreatePackage();
        var details = PackageDetailsDto.From(package);

        Assert.Null(cache.GetDetails(package.Key));

        cache.SetDetails(details, cache.CaptureGeneration());
        var cached = cache.GetDetails(package.Key);

        Assert.NotNull(cached);
        Assert.Equal(details.Name, cached.Name);
        Assert.Equal(details.SignatureStatus, cached.SignatureStatus);
        Assert.Equal(details.RegisteredAt, cached.RegisteredAt);
    }

    [Fact]
    public void GetList_RoundTripsEmptyAndPopulatedLists()
    {
        var clock = new TestClock();
        var cache = CreateCache(clock, maxEntries: 8);
        var summary = PackageSummaryDto.From(new PackageIndexEntry(
            TestPackages.CreateManifest(),
            PackageState.Registered,
            TestPackages.Timestamp));

        Assert.Null(cache.GetList(null));

        cache.SetList(null, Array.Empty<PackageSummaryDto>(), cache.CaptureGeneration());
        Assert.Empty(cache.GetList(null)!);

        cache.SetList(" Widget ", [summary], cache.CaptureGeneration());
        var cached = cache.GetList("widget");

        Assert.NotNull(cached);
        Assert.Equal("widget", Assert.Single(cached).Name);
    }

    [Fact]
    public void ListKeys_DoNotCollideBetweenUnfilteredAndLiteralStarSearch()
    {
        var clock = new TestClock();
        var cache = CreateCache(clock, maxEntries: 8);
        var unfiltered = Summary("unfiltered");
        var filtered = Summary("filtered");

        cache.SetList(null, [unfiltered], cache.CaptureGeneration());
        cache.SetList("*", [filtered], cache.CaptureGeneration());

        Assert.Equal("unfiltered", Assert.Single(cache.GetList(null)!).Name);
        Assert.Equal("filtered", Assert.Single(cache.GetList("*")!).Name);

        cache.InvalidateLists();

        Assert.Null(cache.GetList(null));
        Assert.Null(cache.GetList("*"));
    }

    [Fact]
    public void SetDetails_RejectsFillCapturedBeforeInvalidation()
    {
        var clock = new TestClock();
        var cache = CreateCache(clock, maxEntries: 8);
        var package = TestPackages.CreatePackage();
        var generation = cache.CaptureGeneration();

        cache.InvalidatePackage(package.Key);
        cache.SetDetails(PackageDetailsDto.From(package), generation);
        cache.SetDetails(PackageDetailsDto.From(package), cache.CaptureGeneration());

        Assert.NotNull(cache.GetDetails(package.Key));
    }

    [Fact]
    public void SetList_RejectsFillCapturedBeforeInvalidation()
    {
        var clock = new TestClock();
        var cache = CreateCache(clock, maxEntries: 8);
        var generation = cache.CaptureGeneration();

        cache.InvalidateLists();
        cache.SetList(null, [Summary("stale")], generation);
        cache.SetList(null, [Summary("fresh")], cache.CaptureGeneration());

        Assert.Equal("fresh", Assert.Single(cache.GetList(null)!).Name);
    }

    [Fact]
    public void InvalidatePackage_RemovesDetailsAndLists()
    {
        var clock = new TestClock();
        var cache = CreateCache(clock, maxEntries: 8);
        var package = TestPackages.CreatePackage();
        var details = PackageDetailsDto.From(package);
        var summary = PackageSummaryDto.From(new PackageIndexEntry(
            package.Manifest,
            package.State,
            package.RegisteredAt));

        cache.SetDetails(details, cache.CaptureGeneration());
        cache.SetList(null, [summary], cache.CaptureGeneration());
        cache.InvalidatePackage(package.Key);

        Assert.Null(cache.GetDetails(package.Key));
        Assert.Null(cache.GetList(null));
    }

    [Fact]
    public void InvalidateLists_PreservesDetails()
    {
        var clock = new TestClock();
        var cache = CreateCache(clock, maxEntries: 8);
        var package = TestPackages.CreatePackage();
        var details = PackageDetailsDto.From(package);

        cache.SetDetails(details, cache.CaptureGeneration());
        cache.SetList(null, [], cache.CaptureGeneration());
        cache.InvalidateLists();

        Assert.NotNull(cache.GetDetails(package.Key));
        Assert.Null(cache.GetList(null));
    }

    [Fact]
    public void AbsoluteExpiration_ExpiresEntry()
    {
        var clock = new TestClock();
        var cache = new JsonPackageReadModelCache(
            new PackageReadModelCacheOptions(
                maxEntries: 8,
                absoluteExpiration: TimeSpan.FromSeconds(10),
                slidingExpiration: TimeSpan.FromSeconds(30)),
            clock);
        var package = TestPackages.CreatePackage();

        cache.SetDetails(PackageDetailsDto.From(package), cache.CaptureGeneration());

        clock.UtcNow = clock.UtcNow.AddSeconds(9);
        Assert.NotNull(cache.GetDetails(package.Key));

        clock.UtcNow = clock.UtcNow.AddSeconds(2);
        Assert.Null(cache.GetDetails(package.Key));
    }

    [Fact]
    public void SlidingExpiration_ExtendsAccessWindowUntilInactivity()
    {
        var clock = new TestClock();
        var cache = new JsonPackageReadModelCache(
            new PackageReadModelCacheOptions(
                maxEntries: 8,
                absoluteExpiration: TimeSpan.FromMinutes(10),
                slidingExpiration: TimeSpan.FromSeconds(60)),
            clock);
        var package = TestPackages.CreatePackage();

        cache.SetDetails(PackageDetailsDto.From(package), cache.CaptureGeneration());

        clock.UtcNow = clock.UtcNow.AddSeconds(50);
        Assert.NotNull(cache.GetDetails(package.Key));

        clock.UtcNow = clock.UtcNow.AddSeconds(50);
        Assert.NotNull(cache.GetDetails(package.Key));

        clock.UtcNow = clock.UtcNow.AddSeconds(70);
        Assert.Null(cache.GetDetails(package.Key));
    }

    [Fact]
    public void BoundedCache_EvictsLeastRecentlyUsedEntry()
    {
        var clock = new TestClock();
        var cache = CreateCache(clock, maxEntries: 2);
        var first = TestPackages.CreatePackage(name: "alpha");
        var second = TestPackages.CreatePackage(name: "beta");
        var third = TestPackages.CreatePackage(name: "gamma");

        cache.SetDetails(PackageDetailsDto.From(first), cache.CaptureGeneration());
        cache.SetDetails(PackageDetailsDto.From(second), cache.CaptureGeneration());
        cache.SetDetails(PackageDetailsDto.From(third), cache.CaptureGeneration());

        Assert.Null(cache.GetDetails(first.Key));
        Assert.NotNull(cache.GetDetails(second.Key));
        Assert.NotNull(cache.GetDetails(third.Key));
    }

    private static PackageSummaryDto Summary(string name) =>
        PackageSummaryDto.From(new PackageIndexEntry(
            TestPackages.CreateManifest(name: name),
            PackageState.Registered,
            TestPackages.Timestamp));

    private static JsonPackageReadModelCache CreateCache(TestClock clock, int maxEntries) =>
        new(
            new PackageReadModelCacheOptions(
                maxEntries,
                TimeSpan.FromHours(1),
                TimeSpan.FromMinutes(1)),
            clock);
}
