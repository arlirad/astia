using System.Runtime.Versioning;
using System.Security.Cryptography;
using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Artifacts;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

[SupportedOSPlatform("linux")]
public sealed class RsaSigningPublisherTests
{
    private static readonly byte[] ArtifactBytes = "signed-artifact"u8.ToArray();

    [Fact]
    public async Task RsaSignerAndVerifier_ProveContentHashSignature()
    {
        using var keys = TestKeys.Create();
        var hash = ContentHash.Create(
            Convert.ToHexString(SHA256.HashData(ArtifactBytes)).ToLowerInvariant());
        var signer = new RsaPackageSigner(keys.PrivateKeyPath);
        var verifier = new RsaPackageSignatureVerifier(keys.PublicKeyPath);

        var signing = await signer.SignAsync(hash, CancellationToken.None);

        Assert.True(signing.Signed);
        Assert.True(verifier.Verify(hash, signing.Signature));
        Assert.False(verifier.Verify(
            ContentHash.Create(new string('0', 64)),
            signing.Signature));
    }

    [Fact]
    public async Task VerifyingPublisher_AcceptsMatchingSignedArtifact()
    {
        using var keys = TestKeys.Create();
        var fixture = await SignedFixture.CreateAsync(keys);
        var publisher = new VerifyingPackagePublisher(fixture.Verifier, fixture.Store);

        var result = await publisher.PublishAsync(
            fixture.Manifest,
            fixture.Artifact,
            CancellationToken.None);

        Assert.True(result.Published);
    }

    [Fact]
    public async Task VerifyingPublisher_RejectsWrongPackageHashOrSignature()
    {
        using var keys = TestKeys.Create();
        var fixture = await SignedFixture.CreateAsync(keys);
        var publisher = new VerifyingPackagePublisher(fixture.Verifier, fixture.Store);
        var wrongPackage = fixture.Artifact with
        {
            Package = PackageKey.Create("other", "1.2.3")
        };
        var wrongHash = fixture.Artifact with
        {
            ContentHash = new string('0', 64)
        };
        var wrongSignature = fixture.Artifact with
        {
            Signature = "not-the-manifest-signature"
        };

        Assert.False((await publisher.PublishAsync(
            fixture.Manifest,
            wrongPackage,
            CancellationToken.None)).Published);
        Assert.False((await publisher.PublishAsync(
            fixture.Manifest,
            wrongHash,
            CancellationToken.None)).Published);
        Assert.False((await publisher.PublishAsync(
            fixture.Manifest,
            wrongSignature,
            CancellationToken.None)).Published);
    }

    [Fact]
    public async Task VerifyingPublisher_RejectsMissingArtifactBytes()
    {
        using var keys = TestKeys.Create();
        var fixture = await SignedFixture.CreateAsync(keys);
        var emptyStore = new InMemoryArtifactStore();
        var publisher = new VerifyingPackagePublisher(fixture.Verifier, emptyStore);

        var result = await publisher.PublishAsync(
            fixture.Manifest,
            fixture.Artifact,
            CancellationToken.None);

        Assert.False(result.Published);
    }

    [Fact]
    public void RsaSigner_AcceptsOwnerOnlyPrivateKeyModes()
    {
        using var keys = TestKeys.Create();

        File.SetUnixFileMode(keys.PrivateKeyPath, UnixFileMode.UserRead);
        _ = new RsaPackageSigner(keys.PrivateKeyPath);

        File.SetUnixFileMode(
            keys.PrivateKeyPath,
            UnixFileMode.UserRead | UnixFileMode.UserWrite);
        _ = new RsaPackageSigner(keys.PrivateKeyPath);
    }

    [Fact]
    public void RsaSigner_RejectsGroupOrOtherReadablePrivateKey()
    {
        using var keys = TestKeys.Create();
        File.SetUnixFileMode(
            keys.PrivateKeyPath,
            UnixFileMode.UserRead
            | UnixFileMode.UserWrite
            | UnixFileMode.GroupRead);

        Assert.Throws<InvalidOperationException>(
            () => new RsaPackageSigner(keys.PrivateKeyPath));
    }

    [Fact]
    public void RsaSigner_RejectsWrongPrivateKeyOwner()
    {
        using var keys = TestKeys.Create();

        Assert.Throws<InvalidOperationException>(
            () => RsaKeyFiles.ReadPrivateKey(keys.PrivateKeyPath, uint.MaxValue));
    }

    [Fact]
    public void RsaSigner_RejectsMalformedPem()
    {
        using var keys = TestKeys.Create();
        File.WriteAllText(keys.PrivateKeyPath, "not a PEM key");
        File.SetUnixFileMode(
            keys.PrivateKeyPath,
            UnixFileMode.UserRead | UnixFileMode.UserWrite);

        Assert.ThrowsAny<Exception>(
            () => new RsaPackageSigner(keys.PrivateKeyPath));
    }

    [Fact]
    public void RsaSigner_RejectsWeakKey()
    {
        using var keys = TestKeys.Create();
        using var weak = RSA.Create(1024);
        File.WriteAllText(keys.PrivateKeyPath, weak.ExportPkcs8PrivateKeyPem());
        File.SetUnixFileMode(
            keys.PrivateKeyPath,
            UnixFileMode.UserRead | UnixFileMode.UserWrite);

        Assert.Throws<InvalidOperationException>(
            () => new RsaPackageSigner(keys.PrivateKeyPath));
    }

    [Fact]
    public void RsaVerifier_RejectsSymlinkedPublicKey()
    {
        using var keys = TestKeys.Create();
        var linkPath = Path.Combine(keys.RootDirectory, "public-link.pem");
        File.CreateSymbolicLink(linkPath, keys.PublicKeyPath);

        Assert.Throws<InvalidOperationException>(
            () => new RsaPackageSignatureVerifier(linkPath));
    }

    private sealed class SignedFixture
    {
        public PackageManifest Manifest { get; private set; } = null!;

        public ArtifactMetadata Artifact { get; private set; } = null!;

        public InMemoryArtifactStore Store { get; } = new();

        public RsaPackageSignatureVerifier Verifier { get; private set; } = null!;

        public static async Task<SignedFixture> CreateAsync(TestKeys keys)
        {
            var fixture = new SignedFixture();
            var hash = ContentHash.Create(
                Convert.ToHexString(SHA256.HashData(ArtifactBytes)).ToLowerInvariant());
            var signing = await new RsaPackageSigner(keys.PrivateKeyPath)
                .SignAsync(hash, CancellationToken.None);
            var package = Package.Register(
                TestPackages.CreateManifest(signing.Signature, contentHash: hash.Value),
                BuildPolicy.Create(BuildIsolation.Namespace, false, TimeSpan.FromMinutes(5)),
                TestPackages.Timestamp);
            package.RequestBuild(TestPackages.Timestamp);
            package.AcknowledgeDomainEvents(package.PeekDomainEvents());

            fixture.Manifest = package.Manifest;
            fixture.Verifier = new RsaPackageSignatureVerifier(keys.PublicKeyPath);
            await fixture.Store.StoreAsync(package.Key, ArtifactBytes, CancellationToken.None);
            fixture.Artifact = new ArtifactMetadata(
                package.Key,
                hash.Value,
                signing.Signature.Value,
                TestPackages.Timestamp);
            return fixture;
        }
    }

    private sealed class TestKeys : IDisposable
    {
        private TestKeys(string rootDirectory, string privateKeyPath, string publicKeyPath)
        {
            RootDirectory = rootDirectory;
            PrivateKeyPath = privateKeyPath;
            PublicKeyPath = publicKeyPath;
        }

        public string RootDirectory { get; }

        public string PrivateKeyPath { get; }

        public string PublicKeyPath { get; }

        public static TestKeys Create()
        {
            var directory = Path.Combine(Path.GetTempPath(), $"pkgsrv-keys-{Guid.NewGuid():N}");
            Directory.CreateDirectory(directory);
            using var rsa = RSA.Create(2048);
            var privateKeyPath = Path.Combine(directory, "private.pem");
            var publicKeyPath = Path.Combine(directory, "public.pem");
            File.WriteAllText(privateKeyPath, rsa.ExportPkcs8PrivateKeyPem());
            File.WriteAllText(publicKeyPath, rsa.ExportSubjectPublicKeyInfoPem());
            File.SetUnixFileMode(
                privateKeyPath,
                UnixFileMode.UserRead | UnixFileMode.UserWrite);

            return new TestKeys(directory, privateKeyPath, publicKeyPath);
        }

        public void Dispose()
        {
            Directory.Delete(RootDirectory, recursive: true);
        }
    }
}
