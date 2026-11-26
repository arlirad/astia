using System.Security.Cryptography;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Features.Packages.Download;
using Pkgsrv.Application.Features.Packages.Publish;
using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Application.Interfaces.Caching;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Domain.Packages;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Application.Tests;

public sealed class PackagePublishDownloadHandlerTests
{
    private static readonly byte[] ArtifactBytes = "built-artifact"u8.ToArray();

    [Fact]
    public async Task Publish_SignsVerifiesStoresMetadataAndPublishes()
    {
        var harness = await PublishHarness.CreateAsync();
        var signer = new TestSigner();
        var metadataStore = new TestArtifactMetadataStore();
        var publisher = new TestPublisher();
        var handler = harness.CreatePublishHandler(
            signer,
            publisher,
            metadataStore);

        var result = await handler.Handle(
            new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal(harness.ContentHash.Value, result.ContentHash);
        Assert.Equal("test-signature", result.Signature);
        Assert.Equal(PackageAction.Publish, Assert.Single(harness.Authorization.Actions));
        Assert.NotNull(publisher.LastManifest);
        Assert.True(publisher.LastManifest!.IsPublishable);
        Assert.NotNull(metadataStore.LastSaved);
        Assert.Equal("test-signature", metadataStore.LastSaved!.Signature);
        Assert.True(harness.Package.Manifest.IsPublishable);
        Assert.Equal(PackageState.Published, harness.Package.State);
        Assert.NotNull(harness.Package.PublishedAt);
    }

    [Fact]
    public async Task Publish_Success_NotifiesSignatureThenPublicationStateOnceEach()
    {
        var harness = await PublishHarness.CreateAsync();
        var notifier = new RecordingPackageCacheInvalidationNotifier();
        var publisher = new TestPublisher();
        var notificationsAtPublisher = -1;
        publisher.OnPublish = () =>
            notificationsAtPublisher = notifier.Invalidations.Count;
        var handler = harness.CreatePublishHandler(
            new TestSigner(),
            publisher,
            new TestArtifactMetadataStore(),
            notifier: notifier);

        await handler.Handle(
            new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal(1, notificationsAtPublisher);
        Assert.Equal(2, notifier.Invalidations.Count);
        Assert.All(notifier.Invalidations, invalidation =>
        {
            Assert.Equal(PackageCacheInvalidationScope.Package, invalidation.Scope);
            Assert.Equal(harness.Package.Key, invalidation.Package);
        });
    }

    [Fact]
    public async Task Publish_FailedExternalPublication_DoesNotEmitPublicationNotification()
    {
        var harness = await PublishHarness.CreateAsync();
        var notifier = new RecordingPackageCacheInvalidationNotifier();
        var publisher = new TestPublisher
        {
            Result = PublishResult.Refused("verification failed")
        };
        var notificationsAtPublisher = -1;
        publisher.OnPublish = () =>
            notificationsAtPublisher = notifier.Invalidations.Count;
        var handler = harness.CreatePublishHandler(
            new TestSigner(),
            publisher,
            new TestArtifactMetadataStore(),
            notifier: notifier);

        await Assert.ThrowsAsync<PackagePublishException>(
            async () => await handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));

        Assert.Equal(1, notificationsAtPublisher);
        var invalidation = Assert.Single(notifier.Invalidations);
        Assert.Equal(PackageCacheInvalidationScope.Package, invalidation.Scope);
        Assert.Equal(harness.Package.Key, invalidation.Package);
        Assert.Equal(PackageState.BuildRequested, harness.Package.State);
        Assert.True(harness.Package.Manifest.Signature.IsPublishable);
    }

    [Fact]
    public async Task Publish_RetryAfterPersistedSuccessIsIdempotentAndDoesNotRepublish()
    {
        var harness = await PublishHarness.CreateAsync();
        var signer = new TestSigner();
        var publisher = new TestPublisher();
        var handler = harness.CreatePublishHandler(
            signer,
            publisher,
            new TestArtifactMetadataStore());

        var first = await handler.Handle(
            new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);
        var second = await handler.Handle(
            new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal(first, second);
        Assert.Equal(1, signer.Calls);
        Assert.Equal(1, publisher.Calls);
        Assert.Equal(1, harness.Repository.MarkPublishedCalls);
        Assert.Equal(PackageState.Published, harness.Package.State);
        Assert.NotNull(harness.Package.PublishedAt);
    }

    [Fact]
    public async Task Publish_RetryRefusesTamperedSignature()
    {
        var harness = await PublishHarness.CreateAsync();
        var verifier = new TestSignatureVerifier();
        var handler = harness.CreatePublishHandler(
            new TestSigner(),
            new TestPublisher(),
            new TestArtifactMetadataStore(),
            verifier);
        await handler.Handle(
            new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);
        verifier.Allowed = false;

        await Assert.ThrowsAsync<PackageSignatureInvalidException>(
            async () => await handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));

        Assert.Equal(PackageState.Published, harness.Package.State);
        Assert.Equal(1, harness.Repository.MarkPublishedCalls);
    }

    [Fact]
    public async Task Publish_ConcurrentRetriesAfterPersistedSuccessAreReadOnly()
    {
        var harness = await PublishHarness.CreateAsync();
        var signer = new TestSigner();
        var publisher = new TestPublisher();
        var handler = harness.CreatePublishHandler(
            signer,
            publisher,
            new TestArtifactMetadataStore());
        await handler.Handle(
            new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        var retries = Enumerable.Range(0, 6)
            .Select(_ => handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None).AsTask())
            .ToArray();
        await Task.WhenAll(retries);

        Assert.All(retries, retry => Assert.Equal(
            harness.ContentHash.Value,
            retry.Result.ContentHash));
        Assert.Equal(1, signer.Calls);
        Assert.Equal(1, publisher.Calls);
        Assert.Equal(1, harness.Repository.MarkPublishedCalls);
    }

    [Fact]
    public async Task Publish_RetryRefusesTamperedOrMissingArtifact()
    {
        var harness = await PublishHarness.CreateAsync();
        var handler = harness.CreatePublishHandler(
            new TestSigner(),
            new TestPublisher(),
            new TestArtifactMetadataStore());
        await handler.Handle(
            new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        harness.ArtifactStore.Tamper(harness.Package.Key, harness.ContentHash);
        await Assert.ThrowsAsync<PackageArtifactNotFoundException>(
            async () => await handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));

        harness.ArtifactStore.Remove(harness.Package.Key, harness.ContentHash);
        await Assert.ThrowsAsync<PackageArtifactNotFoundException>(
            async () => await handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    [Fact]
    public async Task PublishAndDownload_DownloadIsDeniedUntilDurablePublicationCommits()
    {
        var harness = await PublishHarness.CreateAsync();
        var publishHandler = harness.CreatePublishHandler(
            new TestSigner(),
            new TestPublisher(),
            new TestArtifactMetadataStore());
        var downloadHandler = new DownloadPackageQueryHandler(
            harness.Repository,
            harness.ArtifactStore,
            new TestSignatureVerifier(),
            harness.Authorization);
        var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        harness.Repository.MarkPublishedBarrier = () => gate.Task;

        var publishTask = publishHandler.Handle(
            new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None).AsTask();

        try
        {
            await harness.Repository.MarkPublishedStarted.Task;

            await Assert.ThrowsAsync<PackageNotPublishedException>(
                async () => await downloadHandler.Handle(
                    new DownloadPackageQuery("widget", "1.2.3", TestPackages.Actor),
                    CancellationToken.None));
        }
        finally
        {
            gate.SetResult();
        }

        await publishTask;

        var download = await downloadHandler.Handle(
            new DownloadPackageQuery("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal(ArtifactBytes, download.Content);
    }

    [Fact]
    public async Task Publish_SuccessAllowsSignedDownload()
    {
        var harness = await PublishHarness.CreateAsync();
        var publishHandler = harness.CreatePublishHandler(
            new TestSigner(),
            new TestPublisher(),
            new TestArtifactMetadataStore());

        await publishHandler.Handle(
            new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        var downloadHandler = new DownloadPackageQueryHandler(
            harness.Repository,
            harness.ArtifactStore,
            new TestSignatureVerifier(),
            harness.Authorization);

        var result = await downloadHandler.Handle(
            new DownloadPackageQuery("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal(ArtifactBytes, result.Content);
    }

    [Fact]
    public async Task Publish_RefusesWhenSigningIsUnavailable()
    {
        var harness = await PublishHarness.CreateAsync();
        var handler = harness.CreatePublishHandler(
            new TestSigner
            {
                Result = new SigningResult(PackageSignature.Pending, "signing disabled")
            },
            new TestPublisher(),
            new TestArtifactMetadataStore());

        await Assert.ThrowsAsync<PackagePublishException>(
            async () => await handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    [Fact]
    public async Task Publish_RefusesWhenPublisherRejects()
    {
        var harness = await PublishHarness.CreateAsync();
        var handler = harness.CreatePublishHandler(
            new TestSigner(),
            new TestPublisher { Result = PublishResult.Refused("verification failed") },
            new TestArtifactMetadataStore());

        await Assert.ThrowsAsync<PackagePublishException>(
            async () => await handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));

        Assert.Equal(PackageState.BuildRequested, harness.Package.State);
        Assert.Null(harness.Package.PublishedAt);
    }

    [Fact]
    public async Task Publish_DatabaseFailureAfterPublisherLeavesDownloadDeniedThenRetrySucceeds()
    {
        var harness = await PublishHarness.CreateAsync();
        var handler = harness.CreatePublishHandler(
            new TestSigner(),
            new TestPublisher(),
            new TestArtifactMetadataStore());
        var downloadHandler = new DownloadPackageQueryHandler(
            harness.Repository,
            harness.ArtifactStore,
            new TestSignatureVerifier(),
            harness.Authorization);
        harness.Repository.ThrowOnMarkPublished = true;

        await Assert.ThrowsAsync<PackageConflictException>(
            async () => await handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));

        Assert.Equal(PackageState.BuildRequested, harness.Package.State);
        Assert.Null(harness.Package.PublishedAt);
        await Assert.ThrowsAsync<PackageNotPublishedException>(
            async () => await downloadHandler.Handle(
                new DownloadPackageQuery("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));

        harness.Repository.ThrowOnMarkPublished = false;
        await handler.Handle(
            new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal(PackageState.Published, harness.Package.State);
        Assert.NotNull(harness.Package.PublishedAt);
        var download = await downloadHandler.Handle(
            new DownloadPackageQuery("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);
        Assert.Equal(ArtifactBytes, download.Content);
    }

    [Fact]
    public async Task Publish_RefusesUnverifiableSignature()
    {
        var harness = await PublishHarness.CreateAsync();
        var handler = harness.CreatePublishHandler(
            new TestSigner(),
            new TestPublisher(),
            new TestArtifactMetadataStore(),
            new TestSignatureVerifier { Allowed = false });

        await Assert.ThrowsAsync<PackageSignatureInvalidException>(
            async () => await handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    [Fact]
    public async Task Publish_ReturnsArtifactNotFoundWhenBuildOutputIsMissing()
    {
        var harness = await PublishHarness.CreateAsync(storeArtifact: false);
        var handler = harness.CreatePublishHandler(
            new TestSigner(),
            new TestPublisher(),
            new TestArtifactMetadataStore());

        await Assert.ThrowsAsync<PackageArtifactNotFoundException>(
            async () => await handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    [Fact]
    public async Task Publish_DeniesWhenPublishIsNotAuthorized()
    {
        var harness = await PublishHarness.CreateAsync();
        harness.Authorization.Allow = false;
        var handler = harness.CreatePublishHandler(
            new TestSigner(),
            new TestPublisher(),
            new TestArtifactMetadataStore());

        await Assert.ThrowsAsync<AuthorizationDeniedException>(
            async () => await handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    [Fact]
    public async Task Download_AuthorizesReadsAndReturnsVerifiedBytes()
    {
        var harness = await DownloadHarness.CreateAsync();
        var handler = new DownloadPackageQueryHandler(
            harness.Repository,
            harness.ArtifactStore,
            new TestSignatureVerifier(),
            harness.Authorization);

        var result = await handler.Handle(
            new DownloadPackageQuery("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal(ArtifactBytes, result.Content);
        Assert.Equal(harness.ContentHash.Value, result.ContentHash);
        Assert.Equal(PackageAction.Read, Assert.Single(harness.Authorization.Actions));
    }

    [Fact]
    public async Task Download_RefusesInvalidSignature()
    {
        var harness = await DownloadHarness.CreateAsync();
        var handler = new DownloadPackageQueryHandler(
            harness.Repository,
            harness.ArtifactStore,
            new TestSignatureVerifier { Allowed = false },
            harness.Authorization);

        await Assert.ThrowsAsync<PackageSignatureInvalidException>(
            async () => await handler.Handle(
                new DownloadPackageQuery("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    [Fact]
    public async Task Download_RefusesUnsignedPackage()
    {
        var harness = await DownloadHarness.CreateAsync(signed: false);
        var handler = new DownloadPackageQueryHandler(
            harness.Repository,
            harness.ArtifactStore,
            new TestSignatureVerifier(),
            harness.Authorization);

        await Assert.ThrowsAsync<PackageNotPublishedException>(
            async () => await handler.Handle(
                new DownloadPackageQuery("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    [Fact]
    public async Task Download_RefusesSignedRegisteredButUnpublishedPackage()
    {
        var harness = await DownloadHarness.CreateAsync(signed: true, publish: false);
        var handler = new DownloadPackageQueryHandler(
            harness.Repository,
            harness.ArtifactStore,
            new TestSignatureVerifier(),
            harness.Authorization);

        await Assert.ThrowsAsync<PackageNotPublishedException>(
            async () => await handler.Handle(
                new DownloadPackageQuery("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    [Fact]
    public async Task Download_DeniesWhenReadIsNotAuthorized()
    {
        var harness = await DownloadHarness.CreateAsync();
        harness.Authorization.Allow = false;
        var handler = new DownloadPackageQueryHandler(
            harness.Repository,
            harness.ArtifactStore,
            new TestSignatureVerifier(),
            harness.Authorization);

        await Assert.ThrowsAsync<AuthorizationDeniedException>(
            async () => await handler.Handle(
                new DownloadPackageQuery("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    private sealed class PublishHarness
    {
        public TestRepository Repository { get; } = new();

        public TestAuthorization Authorization { get; } = new();

        public TestArtifactStore ArtifactStore { get; } = new();

        public ContentHash ContentHash { get; private set; } = null!;

        public Package Package { get; private set; } = null!;

        public static async Task<PublishHarness> CreateAsync(bool storeArtifact = true)
        {
            var harness = new PublishHarness();
            await harness.InitializeAsync(storeArtifact);
            return harness;
        }

        public PublishPackageCommandHandler CreatePublishHandler(
            TestSigner signer,
            TestPublisher publisher,
            TestArtifactMetadataStore metadataStore,
            TestSignatureVerifier? verifier = null,
            RecordingPackageCacheInvalidationNotifier? notifier = null) =>
            new(
                Repository,
                ArtifactStore,
                metadataStore,
                signer,
                verifier ?? new TestSignatureVerifier(),
                publisher,
                Authorization,
                new TestClock(),
                notifier);

        private async Task InitializeAsync(bool storeArtifact)
        {
            ContentHash = ContentHash.Create(
                Convert.ToHexString(SHA256.HashData(ArtifactBytes)).ToLowerInvariant());
            Package = Package.Register(
                TestPackages.CreateManifest(contentHash: ContentHash.Value),
                BuildPolicy.Create(BuildIsolation.Namespace, false, TimeSpan.FromMinutes(5)),
                TestPackages.Timestamp);
            Package.RequestBuild(TestPackages.Timestamp);
            Package.AcknowledgeDomainEvents(Package.PeekDomainEvents());
            await Repository.TryAddAsync(Package, CancellationToken.None);

            if (storeArtifact)
            {
                await ArtifactStore.StoreAsync(Package.Key, ArtifactBytes, CancellationToken.None);
            }
        }
    }

    private sealed class DownloadHarness
    {
        public TestRepository Repository { get; } = new();

        public TestAuthorization Authorization { get; } = new();

        public TestArtifactStore ArtifactStore { get; } = new();

        public ContentHash ContentHash { get; private set; } = null!;

        public static async Task<DownloadHarness> CreateAsync(
            bool signed = true,
            bool publish = true)
        {
            var harness = new DownloadHarness();
            await harness.InitializeAsync(signed, publish);
            return harness;
        }

        private async Task InitializeAsync(bool signed, bool publish)
        {
            ContentHash = ContentHash.Create(
                Convert.ToHexString(SHA256.HashData(ArtifactBytes)).ToLowerInvariant());
            var package = Package.Register(
                TestPackages.CreateManifest(
                    signed ? TestPackages.CreateSignedSignature() : PackageSignature.Pending,
                    contentHash: ContentHash.Value),
                BuildPolicy.Create(BuildIsolation.Namespace, false, TimeSpan.FromMinutes(5)),
                TestPackages.Timestamp);

            if (signed)
            {
                package.RequestBuild(TestPackages.Timestamp);

                if (publish)
                {
                    package.MarkPublished(TestPackages.Timestamp.AddMinutes(1));
                }
            }

            await Repository.TryAddAsync(package, CancellationToken.None);
            await ArtifactStore.StoreAsync(package.Key, ArtifactBytes, CancellationToken.None);
        }
    }
}
