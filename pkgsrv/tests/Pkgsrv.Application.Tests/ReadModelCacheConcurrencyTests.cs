using Pkgsrv.Application.Abstractions.Persistence;
using Pkgsrv.Application.Caching;
using Pkgsrv.Application.Features.Packages.Get;
using Pkgsrv.Application.Features.Packages.List;
using Pkgsrv.Domain.Packages;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Application.Tests;

public sealed class ReadModelCacheConcurrencyTests
{
    [Fact]
    public async Task Details_StaleReadCannotRepopulateAfterBuildStateWrite()
    {
        var registered = TestPackages.CreatePackage();
        var buildRequested = Package.Rehydrate(
            registered.Key,
            registered.Manifest,
            registered.BuildPolicy,
            registered.RegisteredAt,
            PackageState.BuildRequested,
            TestPackages.Timestamp.AddMinutes(1));
        var store = new BlockingStore
        {
            Current = registered,
            BlockNextRead = true
        };
        var cache = CreateCache();
        var handler = new GetPackageQueryHandler(store, new TestAuthorization(), cache);
        var query = new GetPackageQuery("widget", "1.2.3", TestPackages.Actor);

        var inFlight = handler.Handle(query, CancellationToken.None).AsTask();
        await store.ReadStarted.Task;

        store.Current = buildRequested;
        cache.InvalidatePackage(registered.Key);
        store.ReleaseRead.SetResult();

        var stale = await inFlight;
        Assert.Equal(PackageState.Registered.ToString(), stale.State);

        var fresh = await handler.Handle(query, CancellationToken.None);
        Assert.Equal(PackageState.BuildRequested.ToString(), fresh.State);
        Assert.Equal(buildRequested.BuildRequestedAt, fresh.BuildRequestedAt);
    }

    [Fact]
    public async Task List_StaleReadCannotRepopulateAfterBuildStateWrite()
    {
        var registered = TestPackages.CreatePackage();
        var buildRequested = Package.Rehydrate(
            registered.Key,
            registered.Manifest,
            registered.BuildPolicy,
            registered.RegisteredAt,
            PackageState.BuildRequested,
            TestPackages.Timestamp.AddMinutes(1));
        var store = new BlockingStore
        {
            Entries = [new PackageIndexEntry(registered.Manifest, registered.State, registered.RegisteredAt)],
            BlockNextRead = true
        };
        var cache = CreateCache();
        var handler = new ListPackagesQueryHandler(store, new TestAuthorization(), cache);
        var query = new ListPackagesQuery(null, TestPackages.Actor);

        var inFlight = handler.Handle(query, CancellationToken.None).AsTask();
        await store.ReadStarted.Task;

        store.Entries = [new PackageIndexEntry(
            buildRequested.Manifest,
            buildRequested.State,
            buildRequested.RegisteredAt)];
        cache.InvalidatePackage(registered.Key);
        store.ReleaseRead.SetResult();

        var stale = await inFlight;
        Assert.Equal(PackageState.Registered.ToString(), Assert.Single(stale).State);

        var fresh = await handler.Handle(query, CancellationToken.None);
        Assert.Equal(PackageState.BuildRequested.ToString(), Assert.Single(fresh).State);
    }

    private static JsonPackageReadModelCache CreateCache() =>
        new(PackageReadModelCacheOptions.Default, new TestClock());

    private sealed class BlockingStore : IPackageRepository, IPackageIndex
    {
        private readonly object _gate = new();

        public Package Current { get; set; } = TestPackages.CreatePackage();

        public IReadOnlyList<PackageIndexEntry> Entries { get; set; } = Array.Empty<PackageIndexEntry>();

        public bool BlockNextRead { get; set; }

        public TaskCompletionSource ReadStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseRead { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public Task<Package?> FindAsync(PackageKey key, CancellationToken cancellationToken) =>
            ReadAsync<Package?>(() => Current, cancellationToken);

        public Task<bool> TryAddAsync(Package package, CancellationToken cancellationToken) =>
            throw new NotSupportedException();

        public Task UpdateAsync(Package package, CancellationToken cancellationToken) =>
            throw new NotSupportedException();

        public Task<IReadOnlyList<PackageIndexEntry>> SearchAsync(
            string? search,
            CancellationToken cancellationToken) =>
            ReadAsync<IReadOnlyList<PackageIndexEntry>>(
                () => Entries.ToArray(),
                cancellationToken);

        private async Task<T> ReadAsync<T>(Func<T> read, CancellationToken cancellationToken)
        {
            bool block;

            lock (_gate)
            {
                block = BlockNextRead;
                BlockNextRead = false;
            }

            var snapshot = read();

            if (block)
            {
                ReadStarted.TrySetResult();
                await ReleaseRead.Task.WaitAsync(cancellationToken);
            }

            return snapshot;
        }
    }
}
