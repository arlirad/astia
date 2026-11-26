using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging.Abstractions;
using Pkgsrv.Application.Contracts;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Features.Packages.Publish;
using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Application.Interfaces.Caching;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Interfaces.Time;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Packages;
using Pkgsrv.Presentation.Caching;
using Pkgsrv.Presentation.Contracts;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Presentation.Tests;

public sealed class PackageCacheInvalidationTests
{
    private static readonly PackageJsonCacheOptions Options =
        PackageJsonCacheOptions.Create(TimeSpan.FromSeconds(5), 100);

    [Fact]
    public async Task Publish_MutationEvent_InvalidatesEveryLinkedCacheInstance()
    {
        using var firstMemory = CreateMemory();
        using var secondMemory = CreateMemory();
        var first = CreateCache(firstMemory);
        var second = CreateCache(secondMemory);
        var firstLoads = new LoadCounter();
        var secondLoads = new LoadCounter();

        await first.GetListJsonAsync(
            TestPackages.Actor,
            null,
            CreateLoad(firstLoads),
            CancellationToken.None);
        await second.GetListJsonAsync(
            TestPackages.Actor,
            null,
            CreateLoad(secondLoads),
            CancellationToken.None);

        var bus = new FanOutInvalidationPublisher(first, second);
        var notifier = new PackageCacheInvalidationNotifier(
            first,
            bus,
            NullLogger<PackageCacheInvalidationNotifier>.Instance);

        await notifier.NotifyAsync(
            PackageCacheInvalidation.ForPackage(
                PackageKey.Create("widget", "1.2.3")),
            CancellationToken.None);

        var firstJson = await first.GetListJsonAsync(
            TestPackages.Actor,
            null,
            CreateLoad(firstLoads),
            CancellationToken.None);
        var secondJson = await second.GetListJsonAsync(
            TestPackages.Actor,
            null,
            CreateLoad(secondLoads),
            CancellationToken.None);

        Assert.Equal(2, firstLoads.Count);
        Assert.Equal(2, secondLoads.Count);
        Assert.Contains("\"name\":\"fresh\"", firstJson, StringComparison.Ordinal);
        Assert.Contains("\"name\":\"fresh\"", secondJson, StringComparison.Ordinal);
        Assert.Equal(
            PackageCacheInvalidationScope.Package,
            Assert.Single(bus.Published).Scope);
    }

    [Fact]
    public async Task Publish_Failure_LocalInvalidationStillHappensAndWritePathDoesNotThrow()
    {
        using var memory = CreateMemory();
        var cache = CreateCache(memory);
        var loads = new LoadCounter();
        await cache.GetListJsonAsync(
            TestPackages.Actor,
            null,
            CreateLoad(loads),
            CancellationToken.None);

        var notifier = new PackageCacheInvalidationNotifier(
            cache,
            new ThrowingInvalidationPublisher(),
            NullLogger<PackageCacheInvalidationNotifier>.Instance);

        await notifier.NotifyAsync(
            PackageCacheInvalidation.List,
            CancellationToken.None);

        var json = await cache.GetListJsonAsync(
            TestPackages.Actor,
            null,
            CreateLoad(loads),
            CancellationToken.None);

        Assert.Equal(2, loads.Count);
        Assert.Contains("\"name\":\"fresh\"", json, StringComparison.Ordinal);
    }

    [Fact]
    public async Task DuplicateAndConcurrentNotifications_AreHarmlessAndInvalidateOncePerRead()
    {
        using var memory = CreateMemory();
        var cache = CreateCache(memory);
        var loads = new LoadCounter();
        await cache.GetListJsonAsync(
            TestPackages.Actor,
            null,
            CreateLoad(loads),
            CancellationToken.None);

        Parallel.For(0, 512, _ => cache.Invalidate(PackageCacheInvalidation.All));
        cache.Invalidate(PackageCacheInvalidation.List);
        cache.Invalidate(PackageCacheInvalidation.List);

        var json = await cache.GetListJsonAsync(
            TestPackages.Actor,
            null,
            CreateLoad(loads),
            CancellationToken.None);

        Assert.Equal(2, loads.Count);
        Assert.Contains("\"name\":\"fresh\"", json, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task Publish_PersistedSignature_InvalidatesSecondCacheBeforePublisherFailure(
        bool publisherThrows)
    {
        var repository = new InMemoryPackageRepository();
        var package = TestPackages.CreatePackage();
        package.RequestBuild(TestPackages.Timestamp);
        package.AcknowledgeDomainEvents(package.PeekDomainEvents());
        await repository.TryAddAsync(package, CancellationToken.None);

        using var firstMemory = CreateMemory();
        using var secondMemory = CreateMemory();
        var first = CreateCache(firstMemory);
        var second = CreateCache(secondMemory);
        var key = package.Key;

        Func<CancellationToken, Task<PackageResponse>> load = async cancellationToken =>
        {
            var current = await repository.FindAsync(key, cancellationToken);
            return PackageResponse.From(PackageDetailsDto.From(current!));
        };

        var pending = await second.GetPackageJsonAsync(
            TestPackages.Actor,
            key.Name.Value,
            key.Version.ToString(),
            load,
            CancellationToken.None);
        Assert.Contains("\"signatureStatus\":\"Pending\"", pending, StringComparison.Ordinal);

        var bus = new FanOutInvalidationPublisher(first, second);
        var notifier = new PackageCacheInvalidationNotifier(
            first,
            bus,
            NullLogger<PackageCacheInvalidationNotifier>.Instance);
        var publisher = new RecordingPublisher
        {
            Result = publisherThrows
                ? null
                : PublishResult.Refused("external registry rejected the package"),
            Throw = publisherThrows
        };
        var handler = new PublishPackageCommandHandler(
            repository,
            new SingleArtifactStore(key, package.Manifest.ContentHash),
            new NoOpArtifactMetadataStore(),
            new FixedSigner(),
            new AllowingVerifier(),
            publisher,
            new AlwaysAllowAuthorization(),
            new FixedClock(),
            notifier);
        var publish = async () => await handler.Handle(
            new PublishPackageCommand(key.Name.Value, key.Version.ToString(), TestPackages.Actor),
            CancellationToken.None);

        if (publisherThrows)
        {
            await Assert.ThrowsAsync<InvalidOperationException>(publish);
        }
        else
        {
            await Assert.ThrowsAsync<PackagePublishException>(publish);
        }

        Assert.Equal(1, publisher.Calls);
        Assert.Equal(PackageState.BuildRequested, package.State);
        Assert.Null(package.PublishedAt);
        Assert.True(package.Manifest.Signature.IsPublishable);

        var invalidation = Assert.Single(bus.Published);
        Assert.Equal(PackageCacheInvalidationScope.Package, invalidation.Scope);
        Assert.Equal(key, invalidation.Package);

        var refreshed = await second.GetPackageJsonAsync(
            TestPackages.Actor,
            key.Name.Value,
            key.Version.ToString(),
            load,
            CancellationToken.None);

        Assert.Contains("\"signatureStatus\":\"Signed\"", refreshed, StringComparison.Ordinal);
        Assert.Contains("\"state\":\"BuildRequested\"", refreshed, StringComparison.Ordinal);
        Assert.DoesNotContain("\"signatureStatus\":\"Pending\"", refreshed, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Publish_SignatureNotificationFailure_DoesNotBlockExternalPublisher()
    {
        var repository = new InMemoryPackageRepository();
        var package = TestPackages.CreatePackage();
        package.RequestBuild(TestPackages.Timestamp);
        package.AcknowledgeDomainEvents(package.PeekDomainEvents());
        await repository.TryAddAsync(package, CancellationToken.None);

        using var memory = CreateMemory();
        var cache = CreateCache(memory);
        var notifier = new PackageCacheInvalidationNotifier(
            cache,
            new ThrowingInvalidationPublisher(),
            NullLogger<PackageCacheInvalidationNotifier>.Instance);
        var publisher = new RecordingPublisher();
        var handler = new PublishPackageCommandHandler(
            repository,
            new SingleArtifactStore(package.Key, package.Manifest.ContentHash),
            new NoOpArtifactMetadataStore(),
            new FixedSigner(),
            new AllowingVerifier(),
            publisher,
            new AlwaysAllowAuthorization(),
            new FixedClock(),
            notifier);

        var result = await handler.Handle(
            new PublishPackageCommand(
                package.Key.Name.Value,
                package.Key.Version.ToString(),
                TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal(1, publisher.Calls);
        Assert.Equal(PackageState.Published, package.State);
        Assert.NotNull(result.Signature);
    }

    [Fact]
    public async Task Publish_AwaitsSignatureNotificationBeforeExternalPublisherAndPublicationNotificationAfter()
    {
        var repository = new InMemoryPackageRepository();
        var package = TestPackages.CreatePackage();
        package.RequestBuild(TestPackages.Timestamp);
        package.AcknowledgeDomainEvents(package.PeekDomainEvents());
        await repository.TryAddAsync(package, CancellationToken.None);

        using var memory = CreateMemory();
        var cache = CreateCache(memory);
        var order = new List<string>();
        var delayedPublisher = new DelayedInvalidationPublisher(order);
        var notifier = new PackageCacheInvalidationNotifier(
            cache,
            delayedPublisher,
            NullLogger<PackageCacheInvalidationNotifier>.Instance);
        var externalPublisher = new RecordingPublisher();
        externalPublisher.OnPublish = () => order.Add("external-publish");
        var handler = new PublishPackageCommandHandler(
            repository,
            new SingleArtifactStore(package.Key, package.Manifest.ContentHash),
            new NoOpArtifactMetadataStore(),
            new FixedSigner(),
            new AllowingVerifier(),
            externalPublisher,
            new AlwaysAllowAuthorization(),
            new FixedClock(),
            notifier);

        var handleTask = handler.Handle(
            new PublishPackageCommand(
                package.Key.Name.Value,
                package.Key.Version.ToString(),
                TestPackages.Actor),
            CancellationToken.None).AsTask();

        await delayedPublisher.FirstPublishStarted.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.False(handleTask.IsCompleted);
        Assert.Equal(0, externalPublisher.Calls);
        Assert.Equal(1, delayedPublisher.Calls);

        delayedPublisher.ReleaseFirstPublish();
        await handleTask;

        Assert.Equal(2, delayedPublisher.Calls);
        Assert.Equal(
            new[]
            {
                "notify-start:1",
                "notify-end:1",
                "external-publish",
                "notify-start:2",
                "notify-end:2"
            },
            order);
    }

    private static MemoryCache CreateMemory() =>
        new(new MemoryCacheOptions { SizeLimit = Options.SizeLimit });

    private static PackageJsonCache CreateCache(IMemoryCache memory) =>
        new(memory, Options, new AlwaysAllowAuthorization());

    private static Task<PackageListResponse> LoadList(LoadCounter loads)
    {
        loads.Count++;
        var name = loads.Count == 1 ? "stale" : "fresh";

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

    private static Func<CancellationToken, Task<PackageListResponse>> CreateLoad(
        LoadCounter loads) =>
        _ => LoadList(loads);

    private sealed class LoadCounter
    {
        public int Count { get; set; }
    }

    private sealed class FanOutInvalidationPublisher
        : IPackageCacheInvalidationPublisher
    {
        private readonly IPackageCacheInvalidator[] _listeners;

        public FanOutInvalidationPublisher(params IPackageCacheInvalidator[] listeners)
        {
            _listeners = listeners;
        }

        public List<PackageCacheInvalidation> Published { get; } = new();

        public Task PublishAsync(
            PackageCacheInvalidation invalidation,
            CancellationToken cancellationToken)
        {
            Published.Add(invalidation);

            foreach (var listener in _listeners)
            {
                listener.Invalidate(invalidation);
            }

            return Task.CompletedTask;
        }
    }

    private sealed class ThrowingInvalidationPublisher
        : IPackageCacheInvalidationPublisher
    {
        public Task PublishAsync(
            PackageCacheInvalidation invalidation,
            CancellationToken cancellationToken) =>
            Task.FromException(new InvalidOperationException("listener unavailable"));
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

    private sealed class SingleArtifactStore : IArtifactStore
    {
        private readonly PackageKey _key;
        private readonly ContentHash _contentHash;

        public SingleArtifactStore(PackageKey key, ContentHash contentHash)
        {
            _key = key;
            _contentHash = contentHash;
        }

        public Task<StoredArtifact> StoreAsync(
            PackageKey package,
            ReadOnlyMemory<byte> bytes,
            CancellationToken cancellationToken) =>
            throw new NotSupportedException();

        public Task<ArtifactContent?> FindAsync(
            PackageKey package,
            ContentHash contentHash,
            CancellationToken cancellationToken) =>
            Task.FromResult<ArtifactContent?>(
                package == _key && contentHash == _contentHash
                    ? new ArtifactContent(package, contentHash, new byte[] { 1 })
                    : null);
    }

    private sealed class NoOpArtifactMetadataStore : IArtifactMetadataStore
    {
        public Task SaveAsync(
            ArtifactMetadata metadata,
            CancellationToken cancellationToken) =>
            Task.CompletedTask;

        public Task<ArtifactMetadata?> FindAsync(
            PackageKey key,
            CancellationToken cancellationToken) =>
            Task.FromResult<ArtifactMetadata?>(null);
    }

    private sealed class FixedSigner : IPackageSigner
    {
        public Task<SigningResult> SignAsync(
            ContentHash contentHash,
            CancellationToken cancellationToken) =>
            Task.FromResult(new SigningResult(
                TestPackages.CreateSignedSignature(),
                "signed"));
    }

    private sealed class AllowingVerifier : IPackageSignatureVerifier
    {
        public bool Verify(ContentHash contentHash, PackageSignature signature) => true;
    }

    private sealed class RecordingPublisher : IPackagePublisher
    {
        public PublishResult? Result { get; set; } = PublishResult.Success();

        public bool Throw { get; set; }

        public Action? OnPublish { get; set; }

        public int Calls { get; private set; }

        public Task<PublishResult> PublishAsync(
            PackageManifest manifest,
            ArtifactMetadata artifact,
            CancellationToken cancellationToken)
        {
            Calls++;
            OnPublish?.Invoke();

            if (Throw)
            {
                throw new InvalidOperationException("external publisher unavailable");
            }

            return Task.FromResult(Result ?? PublishResult.Success());
        }
    }

    private sealed class DelayedInvalidationPublisher : IPackageCacheInvalidationPublisher
    {
        private readonly List<string> _order;
        private readonly TaskCompletionSource _firstPublishStarted =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly TaskCompletionSource _releaseFirstPublish =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        private int _calls;

        public DelayedInvalidationPublisher(List<string> order)
        {
            _order = order;
        }

        public Task FirstPublishStarted => _firstPublishStarted.Task;

        public int Calls => _calls;

        public void ReleaseFirstPublish() => _releaseFirstPublish.TrySetResult();

        public async Task PublishAsync(
            PackageCacheInvalidation invalidation,
            CancellationToken cancellationToken)
        {
            var call = Interlocked.Increment(ref _calls);
            _order.Add($"notify-start:{call}");

            if (call == 1)
            {
                _firstPublishStarted.TrySetResult();
                await _releaseFirstPublish
                    .Task
                    .WaitAsync(cancellationToken)
                    .ConfigureAwait(false);
            }

            _order.Add($"notify-end:{call}");
        }
    }

    private sealed class FixedClock : ISystemClock
    {
        public DateTimeOffset UtcNow => TestPackages.Timestamp.AddMinutes(1);
    }
}
