using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Internal;
using Pkgsrv.Application.Contracts;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Presentation.Caching;
using Pkgsrv.Presentation.Contracts;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Presentation.Tests;

public sealed class PackageJsonCacheTests
{
    private static readonly PackageJsonCacheOptions Options =
        PackageJsonCacheOptions.Create(TimeSpan.FromSeconds(5), 100);

    [Fact]
    public async Task List_SerializesOnceForSameActorAndNormalizedQuery()
    {
        var clock = new ManualClock();
        using var memory = new MemoryCache(new MemoryCacheOptions
        {
            Clock = clock,
            SizeLimit = Options.SizeLimit
        });
        var cache = new PackageJsonCache(memory, Options);
        var calls = 0;

        Task<PackageListResponse> Load(CancellationToken _)
        {
            calls++;
            return Task.FromResult(CreateListResponse());
        }

        var first = await cache.GetListJsonAsync(TestPackages.Actor, " Widget ", Load, CancellationToken.None);
        var second = await cache.GetListJsonAsync(TestPackages.Actor, "widget", Load, CancellationToken.None);

        Assert.Equal(1, calls);
        Assert.Equal(first, second);
        Assert.Contains("\"packages\"", first, StringComparison.Ordinal);
        Assert.Contains("\"name\":\"widget\"", first, StringComparison.Ordinal);
    }

    [Fact]
    public async Task List_KeysByQueryAndActor()
    {
        var clock = new ManualClock();
        using var memory = new MemoryCache(new MemoryCacheOptions
        {
            Clock = clock,
            SizeLimit = Options.SizeLimit
        });
        var cache = new PackageJsonCache(memory, Options);
        var calls = 0;

        Task<PackageListResponse> Load(CancellationToken _)
        {
            calls++;
            return Task.FromResult(CreateListResponse());
        }

        await cache.GetListJsonAsync(TestPackages.Actor, "widget", Load, CancellationToken.None);
        await cache.GetListJsonAsync(TestPackages.Actor, "libc", Load, CancellationToken.None);
        await cache.GetListJsonAsync(AuthorizationContext.Create("other"), "widget", Load, CancellationToken.None);

        Assert.Equal(3, calls);
    }

    [Fact]
    public async Task List_UnfilteredAndLiteralStarSearchUseDistinctKeys()
    {
        var clock = new ManualClock();
        using var memory = new MemoryCache(new MemoryCacheOptions
        {
            Clock = clock,
            SizeLimit = Options.SizeLimit
        });
        var cache = new PackageJsonCache(memory, Options);
        var calls = 0;

        Task<PackageListResponse> Load(CancellationToken _)
        {
            calls++;
            return Task.FromResult(CreateListResponse(calls == 1 ? "unfiltered" : "star"));
        }

        var unfiltered = await cache.GetListJsonAsync(
            TestPackages.Actor,
            null,
            Load,
            CancellationToken.None);
        var literalStar = await cache.GetListJsonAsync(
            TestPackages.Actor,
            "*",
            Load,
            CancellationToken.None);

        Assert.Equal(2, calls);
        Assert.NotEqual(unfiltered, literalStar);
        Assert.Contains("\"name\":\"unfiltered\"", unfiltered, StringComparison.Ordinal);
        Assert.Contains("\"name\":\"star\"", literalStar, StringComparison.Ordinal);
    }

    [Fact]
    public async Task InvalidationDuringLoad_DoesNotServeStaleJsonOnNextRead()
    {
        var clock = new ManualClock();
        using var memory = new MemoryCache(new MemoryCacheOptions
        {
            Clock = clock,
            SizeLimit = Options.SizeLimit
        });
        var cache = new PackageJsonCache(memory, Options);
        var loadStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseLoad = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        async Task<PackageListResponse> BlockingLoad(CancellationToken cancellationToken)
        {
            loadStarted.TrySetResult();
            await releaseLoad.Task.WaitAsync(cancellationToken);
            return CreateListResponse("stale");
        }

        var inFlight = cache.GetListJsonAsync(
            TestPackages.Actor,
            null,
            BlockingLoad,
            CancellationToken.None);
        await loadStarted.Task;

        cache.InvalidateAfterMutation();
        releaseLoad.SetResult();

        var stale = await inFlight;
        Assert.Contains("\"name\":\"stale\"", stale, StringComparison.Ordinal);

        var fresh = await cache.GetListJsonAsync(
            TestPackages.Actor,
            null,
            _ => Task.FromResult(CreateListResponse("fresh")),
            CancellationToken.None);

        Assert.Contains("\"name\":\"fresh\"", fresh, StringComparison.Ordinal);
    }

    [Fact]
    public async Task ActorKey_DoesNotCollideForDelimiterBearingRoleStrings()
    {
        var clock = new ManualClock();
        using var memory = new MemoryCache(new MemoryCacheOptions
        {
            Clock = clock,
            SizeLimit = Options.SizeLimit
        });
        var cache = new PackageJsonCache(memory, Options);
        var firstActor = AuthorizationContext.Create("alice", ["reader,writer"]);
        var secondActor = AuthorizationContext.Create("alice", ["reader", "writer"]);
        var calls = 0;

        Task<PackageListResponse> Load(CancellationToken _)
        {
            calls++;
            var name = calls == 1 ? "first" : "second";
            return Task.FromResult(new PackageListResponse(
            [
                new PackageSummaryResponse(
                    name,
                    "1.2.3",
                    TestPackages.CommitSha,
                    "gcc 13.2.0",
                    TestPackages.ContentHashValue,
                    "ci://pkgsrv/tests/42",
                    "Pending",
                    "Registered",
                    TestPackages.Timestamp)
            ]));
        }

        var first = await cache.GetListJsonAsync(firstActor, "widget", Load, CancellationToken.None);
        var second = await cache.GetListJsonAsync(secondActor, "widget", Load, CancellationToken.None);

        Assert.Equal(2, calls);
        Assert.NotEqual(first, second);
        Assert.Contains("\"name\":\"first\"", first, StringComparison.Ordinal);
        Assert.Contains("\"name\":\"second\"", second, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Details_SerializesOncePerPackageAndActor()
    {
        var clock = new ManualClock();
        using var memory = new MemoryCache(new MemoryCacheOptions
        {
            Clock = clock,
            SizeLimit = Options.SizeLimit
        });
        var cache = new PackageJsonCache(memory, Options);
        var calls = 0;

        Task<PackageResponse> Load(CancellationToken _)
        {
            calls++;
            return Task.FromResult(CreatePackageResponse());
        }

        var first = await cache.GetPackageJsonAsync(
            TestPackages.Actor,
            "widget",
            "1.2.3",
            Load,
            CancellationToken.None);
        var second = await cache.GetPackageJsonAsync(
            TestPackages.Actor,
            "widget",
            "1.2.3",
            Load,
            CancellationToken.None);

        Assert.Equal(1, calls);
        Assert.Equal(first, second);
        Assert.Contains("\"sourceUrl\"", first, StringComparison.Ordinal);
    }

    [Fact]
    public async Task InvalidateAfterMutation_ForcesReload()
    {
        var clock = new ManualClock();
        using var memory = new MemoryCache(new MemoryCacheOptions
        {
            Clock = clock,
            SizeLimit = Options.SizeLimit
        });
        var cache = new PackageJsonCache(memory, Options);
        var calls = 0;

        Task<PackageListResponse> Load(CancellationToken _)
        {
            calls++;
            return Task.FromResult(CreateListResponse());
        }

        await cache.GetListJsonAsync(TestPackages.Actor, "widget", Load, CancellationToken.None);
        cache.InvalidateAfterMutation();
        await cache.GetListJsonAsync(TestPackages.Actor, "widget", Load, CancellationToken.None);

        Assert.Equal(2, calls);
    }

    [Fact]
    public async Task ExpiredEntries_ReloadAfterTtl()
    {
        var clock = new ManualClock();
        using var memory = new MemoryCache(new MemoryCacheOptions
        {
            Clock = clock,
            SizeLimit = Options.SizeLimit
        });
        var cache = new PackageJsonCache(memory, Options);
        var calls = 0;

        Task<PackageListResponse> Load(CancellationToken _)
        {
            calls++;
            return Task.FromResult(CreateListResponse());
        }

        await cache.GetListJsonAsync(TestPackages.Actor, "widget", Load, CancellationToken.None);
        clock.UtcNow = clock.UtcNow.AddSeconds(6);
        await cache.GetListJsonAsync(TestPackages.Actor, "widget", Load, CancellationToken.None);

        Assert.Equal(2, calls);
    }

    private static PackageListResponse CreateListResponse(string name = "widget") => new(
    [
        new PackageSummaryResponse(
            name,
            "1.2.3",
            TestPackages.CommitSha,
            "gcc 13.2.0",
            TestPackages.ContentHashValue,
            "ci://pkgsrv/tests/42",
            "Pending",
            "Registered",
            TestPackages.Timestamp)
    ]);

    private static PackageResponse CreatePackageResponse() =>
        PackageResponse.From(PackageDetailsDto.From(TestPackages.CreatePackage()));

    private sealed class ManualClock : ISystemClock
    {
        public DateTimeOffset UtcNow { get; set; } = DateTimeOffset.UnixEpoch;
    }
}
