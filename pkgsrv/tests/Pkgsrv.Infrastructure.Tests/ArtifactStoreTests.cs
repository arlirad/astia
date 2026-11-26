using System.Runtime.Versioning;
using System.Security.Cryptography;
using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Artifacts;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

[SupportedOSPlatform("linux")]
public sealed class ArtifactStoreTests
{
    private static readonly UnixFileMode PrivateMode =
        UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute;

    [Fact]
    public async Task FileStore_RoundTripsContentAddressedBytes()
    {
        var root = CreatePrivateRoot();
        var store = new FileSystemArtifactStore(root);
        var package = TestPackages.CreatePackage();
        var bytes = "artifact-bytes"u8.ToArray();

        try
        {
            var stored = await store.StoreAsync(package.Key, bytes, CancellationToken.None);
            var found = await store.FindAsync(package.Key, stored.ContentHash, CancellationToken.None);

            Assert.Equal(bytes, found!.Bytes.ToArray());
            Assert.Equal(Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant(), stored.ContentHash.Value);
            Assert.Contains(
                Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories),
                path => Path.GetFileName(path).Contains(stored.ContentHash.Value, StringComparison.Ordinal));
            Assert.All(
                Directory.EnumerateDirectories(root, "*", SearchOption.AllDirectories),
                directory => Assert.Equal(PrivateMode, File.GetUnixFileMode(directory)));
            Assert.All(
                Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories),
                file => Assert.Equal(
                    UnixFileMode.UserRead | UnixFileMode.UserWrite,
                    File.GetUnixFileMode(file)));
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public async Task FileStore_ReturnsNullWhenBytesNoLongerMatchHash()
    {
        var root = CreatePrivateRoot();
        var store = new FileSystemArtifactStore(root);
        var package = TestPackages.CreatePackage();
        var bytes = "artifact-bytes"u8.ToArray();

        try
        {
            var stored = await store.StoreAsync(package.Key, bytes, CancellationToken.None);
            var path = Directory.EnumerateFiles(root, "*.bin", SearchOption.AllDirectories).Single();
            await File.WriteAllBytesAsync(path, "tampered"u8.ToArray(), CancellationToken.None);

            Assert.Null(await store.FindAsync(package.Key, stored.ContentHash, CancellationToken.None));
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public void FileStore_RequiresAbsoluteRoot()
    {
        Assert.Throws<ArgumentException>(() => new FileSystemArtifactStore("relative/path"));
    }

    [Fact]
    public void FileStore_RequiresPreprovisionedRoot()
    {
        var root = Path.Combine(Path.GetTempPath(), $"pkgsrv-missing-{Guid.NewGuid():N}");

        Assert.Throws<InvalidOperationException>(() => new FileSystemArtifactStore(root));
        Assert.False(Directory.Exists(root));
    }

    [Fact]
    public void FileStore_RejectsGroupOrOtherReadableRoot()
    {
        var root = CreatePrivateRoot();

        try
        {
            File.SetUnixFileMode(
                root,
                PrivateMode | UnixFileMode.GroupRead | UnixFileMode.OtherRead);

            Assert.Throws<InvalidOperationException>(() => new FileSystemArtifactStore(root));
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public void FileStore_RejectsSymlinkedRootAndParentComponent()
    {
        var parent = Path.Combine(Path.GetTempPath(), $"pkgsrv-artifact-parent-{Guid.NewGuid():N}");
        var real = Path.Combine(parent, "real");
        var link = Path.Combine(parent, "link");
        Directory.CreateDirectory(parent);
        File.SetUnixFileMode(parent, PrivateMode);
        Directory.CreateDirectory(real);
        File.SetUnixFileMode(real, PrivateMode);
        Directory.CreateSymbolicLink(link, real);

        try
        {
            Assert.Throws<InvalidOperationException>(() => new FileSystemArtifactStore(link));
        }
        finally
        {
            Directory.Delete(parent, recursive: true);
        }
    }

    [Fact]
    public async Task FileStore_ReturnsNullForSymlinkedArtifactFile()
    {
        var root = CreatePrivateRoot();
        var store = new FileSystemArtifactStore(root);
        var package = TestPackages.CreatePackage();
        var bytes = "artifact-bytes"u8.ToArray();
        var external = Path.Combine(Path.GetTempPath(), $"pkgsrv-external-{Guid.NewGuid():N}");
        File.WriteAllBytes(external, bytes);

        try
        {
            var stored = await store.StoreAsync(package.Key, bytes, CancellationToken.None);
            var path = Directory.EnumerateFiles(root, "*.bin", SearchOption.AllDirectories).Single();
            File.Delete(path);
            File.CreateSymbolicLink(path, external);

            Assert.Null(await store.FindAsync(package.Key, stored.ContentHash, CancellationToken.None));
        }
        finally
        {
            File.Delete(external);
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public async Task InMemoryStore_RoundTrips()
    {
        var store = new InMemoryArtifactStore();
        var package = TestPackages.CreatePackage();
        var bytes = "in-memory"u8.ToArray();

        var stored = await store.StoreAsync(package.Key, bytes, CancellationToken.None);
        var found = await store.FindAsync(package.Key, stored.ContentHash, CancellationToken.None);

        Assert.Equal(bytes, found!.Bytes.ToArray());
    }

    private static string CreatePrivateRoot()
    {
        var root = Path.Combine(Path.GetTempPath(), $"pkgsrv-artifacts-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);
        File.SetUnixFileMode(root, PrivateMode);
        return root;
    }
}
