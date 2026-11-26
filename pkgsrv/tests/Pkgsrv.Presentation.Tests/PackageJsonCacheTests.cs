using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Internal;
using Pkgsrv.Application.Contracts;
using Pkgsrv.Application.Errors;
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
        var cache = CreateCache(memory);
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
        var cache = CreateCache(memory);
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
        var cache = CreateCache(memory);
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
        var cache = CreateCache(memory);
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
        var cache = CreateCache(memory);
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
        var cache = CreateCache(memory);
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
    public async Task List_UsesVerifiedIdentityForCacheKeyNotSpoofedActor()
    {
        var clock = new ManualClock();
        using var memory = new MemoryCache(new MemoryCacheOptions
        {
            Clock = clock,
            SizeLimit = Options.SizeLimit
        });
        var verified = AuthorizationContext.Create(
            "alice",
            accountId: Guid.Parse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"));
        var cache = CreateCache(memory, new VerifiedAuthorization(verified));
        var calls = 0;

        Task<PackageListResponse> Load(CancellationToken _)
        {
            calls++;
            return Task.FromResult(CreateListResponse());
        }

        await cache.GetListJsonAsync(
            AuthorizationContext.Create(
                "spoof-a",
                ["reader"],
                Guid.Parse("11111111-2222-3333-4444-555555555555")),
            null,
            Load,
            CancellationToken.None);
        await cache.GetListJsonAsync(
            AuthorizationContext.Create(
                "spoof-b",
                ["publisher"],
                Guid.Parse("99999999-8888-7777-6666-555555555555")),
            null,
            Load,
            CancellationToken.None);

        Assert.Equal(1, calls);
    }

    [Fact]
    public async Task List_DifferentVerifiedAccountsUseDifferentCacheKeys()
    {
        var clock = new ManualClock();
        using var memory = new MemoryCache(new MemoryCacheOptions
        {
            Clock = clock,
            SizeLimit = Options.SizeLimit
        });
        var first = AuthorizationContext.Create(
            "alice",
            accountId: Guid.Parse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"));
        var second = AuthorizationContext.Create(
            "bob",
            accountId: Guid.Parse("11111111-2222-3333-4444-555555555555"));
        var cache = CreateCache(memory, new VerifiedAuthorization(first, second));
        var calls = 0;

        Task<PackageListResponse> Load(CancellationToken _)
        {
            calls++;
            return Task.FromResult(CreateListResponse(calls == 1 ? "first" : "second"));
        }

        var spoofed = AuthorizationContext.Create("spoofed", ["reader"]);
        var firstJson = await cache.GetListJsonAsync(spoofed, null, Load, CancellationToken.None);
        var secondJson = await cache.GetListJsonAsync(spoofed, null, Load, CancellationToken.None);

        Assert.Equal(2, calls);
        Assert.NotEqual(firstJson, secondJson);
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
        var cache = CreateCache(memory);
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
        var cache = CreateCache(memory);
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

    [Fact]
    public async Task ListCacheHit_StillRequiresAuthorization()
    {
        var clock = new ManualClock();
        using var memory = new MemoryCache(new MemoryCacheOptions
        {
            Clock = clock,
            SizeLimit = Options.SizeLimit
        });
        var authorization = new ToggleAuthorization();
        var cache = CreateCache(memory, authorization);
        var calls = 0;

        Task<PackageListResponse> Load(CancellationToken _)
        {
            calls++;
            return Task.FromResult(CreateListResponse());
        }

        await cache.GetListJsonAsync(TestPackages.Actor, null, Load, CancellationToken.None);
        authorization.Allow = false;

        await Assert.ThrowsAsync<AuthorizationDeniedException>(
            () => cache.GetListJsonAsync(TestPackages.Actor, null, Load, CancellationToken.None));

        Assert.Equal(1, calls);
        Assert.Equal(2, authorization.Calls);
    }

    [Fact]
    public async Task PackageCacheHit_StillRequiresAuthorization()
    {
        var clock = new ManualClock();
        using var memory = new MemoryCache(new MemoryCacheOptions
        {
            Clock = clock,
            SizeLimit = Options.SizeLimit
        });
        var authorization = new ToggleAuthorization();
        var cache = CreateCache(memory, authorization);
        var calls = 0;

        Task<PackageResponse> Load(CancellationToken _)
        {
            calls++;
            return Task.FromResult(CreatePackageResponse());
        }

        await cache.GetPackageJsonAsync(
            TestPackages.Actor,
            "widget",
            "1.2.3",
            Load,
            CancellationToken.None);
        authorization.Allow = false;

        await Assert.ThrowsAsync<AuthorizationDeniedException>(
            () => cache.GetPackageJsonAsync(
                TestPackages.Actor,
                "widget",
                "1.2.3",
                Load,
                CancellationToken.None));

        Assert.Equal(1, calls);
        Assert.Equal(2, authorization.Calls);
    }

    private static PackageJsonCache CreateCache(
        IMemoryCache memory,
        IIrvaAuthorization? authorization = null) =>
        new(memory, Options, authorization ?? new AlwaysAllowAuthorization());

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

    private sealed class AlwaysAllowAuthorization : IIrvaAuthorization
    {
        public Task<AuthorizationResult> AuthorizeAsync(
            AuthorizationContext context,
            PackageAction action,
            PackageKey? package,
            CancellationToken cancellationToken) =>
            Task.FromResult(AuthorizationResult.Allow());
    }

    private sealed class VerifiedAuthorization : IIrvaAuthorization
    {
        private readonly AuthorizationContext[] _contexts;
        private int _calls;

        public VerifiedAuthorization(params AuthorizationContext[] contexts)
        {
            _contexts = contexts;
        }

        public Task<AuthorizationResult> AuthorizeAsync(
            AuthorizationContext context,
            PackageAction action,
            PackageKey? package,
            CancellationToken cancellationToken)
        {
            var verified = _contexts[Math.Min(_calls++, _contexts.Length - 1)];
            return Task.FromResult(
                AuthorizationResult.Allow(verified, "verified"));
        }
    }

    private sealed class ToggleAuthorization : IIrvaAuthorization
    {
        public bool Allow { get; set; } = true;

        public int Calls { get; private set; }

        public Task<AuthorizationResult> AuthorizeAsync(
            AuthorizationContext context,
            PackageAction action,
            PackageKey? package,
            CancellationToken cancellationToken)
        {
            Calls++;
            return Task.FromResult(Allow
                ? AuthorizationResult.Allow()
                : AuthorizationResult.Deny("permission revoked"));
        }
    }
}
