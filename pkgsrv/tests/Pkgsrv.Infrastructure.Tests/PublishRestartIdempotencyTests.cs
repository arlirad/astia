using System.Runtime.Versioning;
using System.Security.Cryptography;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Features.Packages.Publish;
using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Interfaces.Time;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Artifacts;
using Pkgsrv.Infrastructure.Packages;
using Pkgsrv.Infrastructure.Persistence.Postgres;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

[SupportedOSPlatform("linux")]
public sealed class PublishRestartIdempotencyTests
{
    private static readonly byte[] ArtifactBytes = "restart-artifact"u8.ToArray();

    [Fact]
    public async Task RetryAfterRestartWithEmptyMetadataSucceedsFromDurableStateAndArtifact()
    {
        var package = CreatePublishedPackage();
        var repository = await CreateRepositoryAsync(package);
        var artifacts = new InMemoryArtifactStore();
        await artifacts.StoreAsync(package.Key, ArtifactBytes, CancellationToken.None);

        var metadata = new InMemoryArtifactMetadataStore();
        await metadata.SaveAsync(
            new ArtifactMetadata(
                package.Key,
                package.Manifest.ContentHash.Value,
                package.Manifest.Signature.Value,
                TestPackages.Timestamp),
            CancellationToken.None);
        Assert.NotNull(await metadata.FindAsync(package.Key, CancellationToken.None));

        var restartedMetadata = new ThrowingArtifactMetadataStore();
        var signer = new ThrowingSigner();
        var publisher = new ThrowingPublisher();
        var handler = CreateHandler(repository, artifacts, restartedMetadata, signer, publisher);

        var result = await handler.Handle(
            new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal(package.Manifest.ContentHash.Value, result.ContentHash);
        Assert.Equal(package.Manifest.Signature.Value, result.Signature);
        Assert.Equal(0, signer.Calls);
        Assert.Equal(0, publisher.Calls);
        Assert.Equal(0, repository.MarkPublishedCalls);
        Assert.Equal(0, repository.UpdateCalls);
        Assert.Equal(0, repository.UpdateSignatureCalls);
        Assert.Equal(PackageState.Published, package.State);
        Assert.Equal(TestPackages.Timestamp.AddMinutes(1), package.PublishedAt);
    }

    [Fact]
    public async Task RetryFailsClosedWhenArtifactIsMissing()
    {
        var package = CreatePublishedPackage();
        var repository = await CreateRepositoryAsync(package);
        var signer = new ThrowingSigner();
        var publisher = new ThrowingPublisher();
        var handler = CreateHandler(
            repository,
            new InMemoryArtifactStore(),
            new ThrowingArtifactMetadataStore(),
            signer,
            publisher);

        await Assert.ThrowsAsync<PackageArtifactNotFoundException>(
            async () => await handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));

        Assert.Equal(0, signer.Calls);
        Assert.Equal(0, publisher.Calls);
        Assert.Equal(0, repository.MarkPublishedCalls);
    }

    [Fact]
    public async Task RetryFailsClosedWhenArtifactBytesAreTampered()
    {
        var package = CreatePublishedPackage();
        var repository = await CreateRepositoryAsync(package);
        var root = Path.Combine(Path.GetTempPath(), $"pkgsrv-retry-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);
        File.SetUnixFileMode(
            root,
            UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);

        try
        {
            var artifacts = new FileSystemArtifactStore(root);
            await artifacts.StoreAsync(package.Key, ArtifactBytes, CancellationToken.None);
            var hash = package.Manifest.ContentHash.Value;
            File.WriteAllText(
                Path.Combine(root, package.Key.Name.Value, package.Key.Version.ToString(), $"{hash}.bin"),
                "tampered");

            var handler = CreateHandler(
                repository,
                artifacts,
                new ThrowingArtifactMetadataStore(),
                new ThrowingSigner(),
                new ThrowingPublisher());

            await Assert.ThrowsAsync<PackageArtifactNotFoundException>(
                async () => await handler.Handle(
                    new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                    CancellationToken.None));
        }
        finally
        {
            if (Directory.Exists(root))
            {
                Directory.Delete(root, recursive: true);
            }
        }
    }

    [Fact]
    public async Task RetryFailsClosedWhenManifestSignatureDoesNotVerify()
    {
        var package = CreatePublishedPackage();
        var repository = await CreateRepositoryAsync(package);
        var artifacts = new InMemoryArtifactStore();
        await artifacts.StoreAsync(package.Key, ArtifactBytes, CancellationToken.None);
        var signer = new ThrowingSigner();
        var publisher = new ThrowingPublisher();
        var handler = CreateHandler(
            repository,
            artifacts,
            new ThrowingArtifactMetadataStore(),
            signer,
            publisher,
            new RejectingVerifier());

        await Assert.ThrowsAsync<PackageSignatureInvalidException>(
            async () => await handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));

        Assert.Equal(0, signer.Calls);
        Assert.Equal(0, publisher.Calls);
        Assert.Equal(0, repository.MarkPublishedCalls);
    }

    [Fact]
    public async Task RetryFailsClosedWhenArtifactBelongsToAnotherPackage()
    {
        var package = CreatePublishedPackage();
        var repository = await CreateRepositoryAsync(package);
        var otherKey = PackageKey.Create("other", "9.9.9");
        var artifacts = new FixedArtifactStore(
            new ArtifactContent(otherKey, package.Manifest.ContentHash, ArtifactBytes));
        var handler = CreateHandler(
            repository,
            artifacts,
            new ThrowingArtifactMetadataStore(),
            new ThrowingSigner(),
            new ThrowingPublisher());

        await Assert.ThrowsAsync<PackageArtifactNotFoundException>(
            async () => await handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    [Fact]
    public async Task RetryFailsClosedWhenArtifactContentHashDiffersFromPersistedHash()
    {
        var package = CreatePublishedPackage();
        var repository = await CreateRepositoryAsync(package);
        var artifacts = new FixedArtifactStore(
            new ArtifactContent(
                package.Key,
                ContentHash.Create(new string('a', 64)),
                ArtifactBytes));
        var handler = CreateHandler(
            repository,
            artifacts,
            new ThrowingArtifactMetadataStore(),
            new ThrowingSigner(),
            new ThrowingPublisher());

        await Assert.ThrowsAsync<PackageArtifactNotFoundException>(
            async () => await handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    [Fact]
    public async Task RetryFailsClosedWhenPersistedPublishedStateIsInconsistent()
    {
        var contentHash = ContentHash.Create(Hash(ArtifactBytes));
        var inconsistent = Package.Rehydrate(
            TestPackages.CreateManifest(PackageSignature.Pending, contentHash: contentHash.Value),
            BuildPolicy.Create(BuildIsolation.Container, false, TimeSpan.FromMinutes(5)),
            PackageState.Published,
            TestPackages.Timestamp,
            TestPackages.Timestamp,
            pendingDomainEvents: null,
            publishedAt: TestPackages.Timestamp.AddMinutes(1));
        var repository = await CreateRepositoryAsync(inconsistent);
        var artifacts = new InMemoryArtifactStore();
        await artifacts.StoreAsync(inconsistent.Key, ArtifactBytes, CancellationToken.None);
        var handler = CreateHandler(
            repository,
            artifacts,
            new ThrowingArtifactMetadataStore(),
            new ThrowingSigner(),
            new ThrowingPublisher());

        await Assert.ThrowsAsync<PackageSignatureInvalidException>(
            async () => await handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    [Fact]
    public async Task ConcurrentRetriesAfterRestartAreReadOnlyAndReturnSameResult()
    {
        var package = CreatePublishedPackage();
        var repository = await CreateRepositoryAsync(package);
        var artifacts = new InMemoryArtifactStore();
        await artifacts.StoreAsync(package.Key, ArtifactBytes, CancellationToken.None);
        var signer = new ThrowingSigner();
        var publisher = new ThrowingPublisher();
        var handler = CreateHandler(
            repository,
            artifacts,
            new ThrowingArtifactMetadataStore(),
            signer,
            publisher);

        var retries = Enumerable.Range(0, 8)
            .Select(_ => handler.Handle(
                new PublishPackageCommand("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None).AsTask())
            .ToArray();
        await Task.WhenAll(retries);

        Assert.All(retries, retry =>
        {
            Assert.Equal(package.Manifest.ContentHash.Value, retry.Result.ContentHash);
            Assert.Equal(package.Manifest.Signature.Value, retry.Result.Signature);
        });
        Assert.Equal(0, signer.Calls);
        Assert.Equal(0, publisher.Calls);
        Assert.Equal(0, repository.MarkPublishedCalls);
        Assert.Equal(0, repository.UpdateCalls);
        Assert.Equal(0, repository.UpdateSignatureCalls);
    }

    private static Package CreatePublishedPackage()
    {
        var contentHash = ContentHash.Create(Hash(ArtifactBytes));
        var package = Package.Register(
            TestPackages.CreateManifest(
                TestPackages.CreateSignedSignature(),
                contentHash: contentHash.Value),
            BuildPolicy.Create(BuildIsolation.Container, false, TimeSpan.FromMinutes(5)),
            TestPackages.Timestamp);
        package.RequestBuild(TestPackages.Timestamp);
        package.MarkPublished(TestPackages.Timestamp.AddMinutes(1));

        return PostgresPackageMapper.ToDomain(PostgresPackageMapper.FromDomain(package));
    }

    private static async Task<CountingPackageRepository> CreateRepositoryAsync(Package package)
    {
        var repository = new CountingPackageRepository();
        await repository.TryAddAsync(package, CancellationToken.None);
        return repository;
    }

    private static PublishPackageCommandHandler CreateHandler(
        IPackageRepository repository,
        IArtifactStore artifactStore,
        IArtifactMetadataStore metadataStore,
        IPackageSigner signer,
        IPackagePublisher publisher,
        IPackageSignatureVerifier? verifier = null) =>
        new(
            repository,
            artifactStore,
            metadataStore,
            signer,
            verifier ?? new AllowingVerifier(),
            publisher,
            new AllowingAuthorization(),
            new FixedClock());

    private static string Hash(ReadOnlySpan<byte> bytes) =>
        Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

    private sealed class CountingPackageRepository : IPackageRepository
    {
        private readonly InMemoryPackageRepository _inner = new();

        public int MarkPublishedCalls { get; private set; }

        public int UpdateCalls { get; private set; }

        public int UpdateSignatureCalls { get; private set; }

        public Task<Package?> FindAsync(PackageKey key, CancellationToken cancellationToken) =>
            _inner.FindAsync(key, cancellationToken);

        public Task<bool> TryAddAsync(Package package, CancellationToken cancellationToken) =>
            _inner.TryAddAsync(package, cancellationToken);

        public Task UpdateAsync(Package package, CancellationToken cancellationToken)
        {
            UpdateCalls++;
            return _inner.UpdateAsync(package, cancellationToken);
        }

        public Task UpdateSignatureAsync(Package package, CancellationToken cancellationToken)
        {
            UpdateSignatureCalls++;
            return _inner.UpdateSignatureAsync(package, cancellationToken);
        }

        public Task MarkPublishedAsync(
            Package package,
            DateTimeOffset publishedAt,
            CancellationToken cancellationToken)
        {
            MarkPublishedCalls++;
            return _inner.MarkPublishedAsync(package, publishedAt, cancellationToken);
        }

        public Task AcknowledgeDomainEventsAsync(
            PackageKey key,
            IReadOnlyCollection<IDomainEvent> publishedEvents,
            CancellationToken cancellationToken) =>
            _inner.AcknowledgeDomainEventsAsync(key, publishedEvents, cancellationToken);
    }

    private sealed class FixedArtifactStore : IArtifactStore
    {
        private readonly ArtifactContent? _content;

        public FixedArtifactStore(ArtifactContent? content)
        {
            _content = content;
        }

        public Task<StoredArtifact> StoreAsync(
            PackageKey package,
            ReadOnlyMemory<byte> bytes,
            CancellationToken cancellationToken) =>
            throw new InvalidOperationException("Store is not used by the retry path.");

        public Task<ArtifactContent?> FindAsync(
            PackageKey package,
            ContentHash contentHash,
            CancellationToken cancellationToken) =>
            Task.FromResult(_content);
    }

    private sealed class ThrowingArtifactMetadataStore : IArtifactMetadataStore
    {
        public Task SaveAsync(ArtifactMetadata metadata, CancellationToken cancellationToken) =>
            throw new InvalidOperationException("Metadata must not be saved by the retry path.");

        public Task<ArtifactMetadata?> FindAsync(PackageKey key, CancellationToken cancellationToken) =>
            throw new InvalidOperationException("Metadata must not be read by the retry path.");
    }

    private sealed class ThrowingSigner : IPackageSigner
    {
        public int Calls { get; private set; }

        public Task<SigningResult> SignAsync(ContentHash contentHash, CancellationToken cancellationToken)
        {
            Calls++;
            throw new InvalidOperationException("Signer must not run for an already-published package.");
        }
    }

    private sealed class ThrowingPublisher : IPackagePublisher
    {
        public int Calls { get; private set; }

        public Task<PublishResult> PublishAsync(
            PackageManifest manifest,
            ArtifactMetadata artifact,
            CancellationToken cancellationToken)
        {
            Calls++;
            throw new InvalidOperationException("Publisher must not run for an already-published package.");
        }
    }

    private sealed class AllowingVerifier : IPackageSignatureVerifier
    {
        public bool Verify(ContentHash contentHash, PackageSignature signature) => true;
    }

    private sealed class RejectingVerifier : IPackageSignatureVerifier
    {
        public bool Verify(ContentHash contentHash, PackageSignature signature) => false;
    }

    private sealed class AllowingAuthorization : IIrvaAuthorization
    {
        public Task<AuthorizationResult> AuthorizeAsync(
            AuthorizationContext context,
            PackageAction action,
            PackageKey? package,
            CancellationToken cancellationToken) =>
            Task.FromResult(AuthorizationResult.Allow());
    }

    private sealed class FixedClock : ISystemClock
    {
        public DateTimeOffset UtcNow => TestPackages.Timestamp;
    }
}
