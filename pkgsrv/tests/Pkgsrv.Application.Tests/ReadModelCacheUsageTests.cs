using Pkgsrv.Application.Abstractions.Caching;
using Pkgsrv.Application.Abstractions.Persistence;
using Pkgsrv.Application.Abstractions.Security;
using Pkgsrv.Application.Contracts;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Features.Packages.Get;
using Pkgsrv.Application.Features.Packages.List;
using Pkgsrv.Application.Features.Packages.Register;
using Pkgsrv.Application.Features.Packages.RequestBuild;
using Pkgsrv.Domain.Errors;
using Pkgsrv.Domain.Packages;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Application.Tests;

public sealed class ReadModelCacheUsageTests
{
    [Fact]
    public async Task Get_UsesCachedDetailsAfterAuthorization()
    {
        var cache = new TestReadModelCache
        {
            Details = PackageDetailsDto.From(TestPackages.CreatePackage())
        };
        var authorization = new TestAuthorization();
        var handler = new GetPackageQueryHandler(new TestRepository(), authorization, cache);

        var result = await handler.Handle(
            new GetPackageQuery("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal("widget", result.Name);
        Assert.Equal(PackageAction.Read, Assert.Single(authorization.Actions));
    }

    [Fact]
    public async Task Get_DoesNotServeCachedDetailsWhenAuthorizationDenies()
    {
        var cache = new TestReadModelCache
        {
            Details = PackageDetailsDto.From(TestPackages.CreatePackage())
        };
        var authorization = new TestAuthorization { Allow = false };
        var handler = new GetPackageQueryHandler(new TestRepository(), authorization, cache);

        await Assert.ThrowsAsync<AuthorizationDeniedException>(
            async () => await handler.Handle(
                new GetPackageQuery("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    [Fact]
    public async Task Get_StoresDetailsOnMiss()
    {
        var repository = new TestRepository();
        await repository.TryAddAsync(TestPackages.CreatePackage(), CancellationToken.None);
        var cache = new TestReadModelCache();
        var handler = new GetPackageQueryHandler(repository, new TestAuthorization(), cache);

        await handler.Handle(
            new GetPackageQuery("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal(1, cache.SetDetailsCalls);
        Assert.Equal("widget", cache.Details!.Name);
    }

    [Fact]
    public async Task List_UsesCachedListAfterAuthorization()
    {
        var cached = PackageSummaryDto.From(new PackageIndexEntry(
            TestPackages.CreateManifest(name: "cached"),
            PackageState.Registered,
            TestPackages.Timestamp));
        var cache = new TestReadModelCache { List = [cached] };
        var authorization = new TestAuthorization();
        var handler = new ListPackagesQueryHandler(new TestRepository(), authorization, cache);

        var packages = await handler.Handle(
            new ListPackagesQuery(null, TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal("cached", Assert.Single(packages).Name);
        Assert.Equal(PackageAction.Read, Assert.Single(authorization.Actions));
    }

    [Fact]
    public async Task Register_InvalidatesPackageAndListsOnSuccess()
    {
        var harness = new RegistrationHarness();

        await harness.Handler.Handle(
            TestPackages.CreateRegistrationCommand(),
            CancellationToken.None);

        Assert.Equal(
            PackageKey.Create("widget", "1.2.3"),
            Assert.Single(harness.Cache.InvalidatedPackages));
        Assert.Equal(0, harness.Cache.InvalidateListsCalls);
    }

    [Fact]
    public async Task Register_DoesNotInvalidateWhenPackageConflicts()
    {
        var harness = new RegistrationHarness();
        await harness.Repository.TryAddAsync(TestPackages.CreatePackage(), CancellationToken.None);

        await Assert.ThrowsAsync<PackageConflictException>(
            async () => await harness.Handler.Handle(
                TestPackages.CreateRegistrationCommand(),
                CancellationToken.None));

        Assert.Empty(harness.Cache.InvalidatedPackages);
    }

    [Fact]
    public async Task Build_InvalidatesPackageAndListsOnSuccess()
    {
        var repository = new TestRepository();
        var package = TestPackages.CreatePackage();
        await repository.TryAddAsync(package, CancellationToken.None);
        package.AcknowledgeDomainEvents(package.PeekDomainEvents());
        var cache = new TestReadModelCache();
        var handler = new RequestPackageBuildCommandHandler(
            repository,
            new TestBuildSandbox(),
            new TestAuthorization(),
            new TestEventPublisher(),
            new TestClock(),
            cache);

        await handler.Handle(
            new RequestPackageBuildCommand("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal(package.Key, Assert.Single(cache.InvalidatedPackages));
    }

    [Fact]
    public async Task Build_DoesNotInvalidateWhenStateTransitionIsRejected()
    {
        var repository = new TestRepository();
        await repository.TryAddAsync(TestPackages.CreatePackage(), CancellationToken.None);
        var cache = new TestReadModelCache();
        var handler = new RequestPackageBuildCommandHandler(
            repository,
            new TestBuildSandbox(),
            new TestAuthorization(),
            new TestEventPublisher(),
            new TestClock(),
            cache);
        var command = new RequestPackageBuildCommand("widget", "1.2.3", TestPackages.Actor);

        await handler.Handle(command, CancellationToken.None);
        cache.InvalidatedPackages.Clear();

        await Assert.ThrowsAsync<DomainException>(
            async () => await handler.Handle(command, CancellationToken.None));

        Assert.Empty(cache.InvalidatedPackages);
    }

    private sealed class RegistrationHarness
    {
        public TestRepository Repository { get; } = new();

        public TestAuthorization Authorization { get; } = new();

        public TestSourceAllowlist Allowlist { get; } = new();

        public TestEventPublisher Events { get; } = new();

        public TestClock Clock { get; } = new();

        public TestReadModelCache Cache { get; } = new();

        public RegisterPackageCommandHandler Handler => new(
            Repository,
            Allowlist,
            Authorization,
            Events,
            Clock,
            Cache);
    }

    private sealed class TestReadModelCache : IPackageReadModelCache
    {
        private long _generation;

        public PackageDetailsDto? Details { get; set; }

        public IReadOnlyList<PackageSummaryDto>? List { get; set; }

        public List<PackageKey> InvalidatedPackages { get; } = new();

        public int InvalidateListsCalls { get; private set; }

        public int SetDetailsCalls { get; private set; }

        public long CaptureGeneration() => _generation;

        public PackageDetailsDto? GetDetails(PackageKey key) => Details;

        public void SetDetails(PackageDetailsDto package, long generation)
        {
            if (generation != _generation)
            {
                return;
            }

            Details = package;
            SetDetailsCalls++;
        }

        public IReadOnlyList<PackageSummaryDto>? GetList(string? search) => List;

        public void SetList(string? search, IReadOnlyList<PackageSummaryDto> packages, long generation)
        {
            if (generation == _generation)
            {
                List = packages;
            }
        }

        public void InvalidatePackage(PackageKey key)
        {
            _generation++;
            InvalidatedPackages.Add(key);
        }

        public void InvalidateLists()
        {
            _generation++;
            InvalidateListsCalls++;
        }
    }
}
