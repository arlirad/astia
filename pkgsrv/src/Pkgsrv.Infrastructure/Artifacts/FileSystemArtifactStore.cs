#pragma warning disable CA1416
using System.Security.Cryptography;
using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Builds;

namespace Pkgsrv.Infrastructure.Artifacts;

public sealed class FileSystemArtifactStore : IDurableArtifactStore
{
    private const long MaximumArtifactBytes = 1024L * 1024 * 1024;
    private const UnixFileMode PrivateDirectoryMode =
        UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute;
    private const UnixFileMode PrivateFileMode =
        UnixFileMode.UserRead | UnixFileMode.UserWrite;

    private readonly string _root;

    public FileSystemArtifactStore(string root)
    {
        if (string.IsNullOrWhiteSpace(root) || !Path.IsPathFullyQualified(root))
        {
            throw new ArgumentException("Artifact root must be an absolute path.", nameof(root));
        }

        if (!OperatingSystem.IsLinux())
        {
            throw new PlatformNotSupportedException(
                "File-backed artifact storage requires a Linux service account.");
        }

        var fullRoot = Path.GetFullPath(root);

        if (!Directory.Exists(fullRoot))
        {
            throw new InvalidOperationException(
                "Artifact root must be pre-provisioned by the operator and must already exist.");
        }

        if (!IsSafeRoot(fullRoot))
        {
            throw new InvalidOperationException(
                "Artifact root must be service-owned with mode 0700 and must not contain symlink components.");
        }

        _root = fullRoot;
    }

    public async Task<StoredArtifact> StoreAsync(
        PackageKey package,
        ReadOnlyMemory<byte> bytes,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(package);
        EnsureRootSafe();

        var hash = Convert.ToHexString(SHA256.HashData(bytes.Span)).ToLowerInvariant();
        var directory = EnsurePackageDirectory(package);
        var finalPath = EnsureUnderRoot(Path.Combine(directory, $"{hash}.bin"));
        var temporaryPath = EnsureUnderRoot(Path.Combine(directory, $".{Guid.NewGuid():N}.tmp"));

        try
        {
            await using (var stream = new FileStream(
                temporaryPath,
                new FileStreamOptions
                {
                    Mode = FileMode.CreateNew,
                    Access = FileAccess.Write,
                    Share = FileShare.None,
                    BufferSize = 81920,
                    Options = FileOptions.WriteThrough | FileOptions.Asynchronous,
                    UnixCreateMode = PrivateFileMode
                }))
            {
                await stream.WriteAsync(bytes, cancellationToken).ConfigureAwait(false);
                await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
            }

            try
            {
                File.Move(temporaryPath, finalPath, overwrite: false);
            }
            catch (IOException) when (File.Exists(finalPath))
            {
                File.Delete(temporaryPath);
            }
        }
        catch
        {
            if (File.Exists(temporaryPath))
            {
                File.Delete(temporaryPath);
            }

            throw;
        }

        EnsureArtifactFileSafe(finalPath);
        return new StoredArtifact(package, ContentHash.Create(hash), bytes.Length);
    }

    public Task<ArtifactContent?> FindAsync(
        PackageKey package,
        ContentHash contentHash,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(package);
        ArgumentNullException.ThrowIfNull(contentHash);

        try
        {
            EnsureRootSafe();
            var directory = TryGetPackageDirectory(package);

            if (directory is null)
            {
                return Task.FromResult<ArtifactContent?>(null);
            }

            var path = EnsureUnderRoot(Path.Combine(directory, $"{contentHash.Value}.bin"));

            if (!UnixFileMetadata.TryOpenRegularFile(path, out var handle, out var length)
                || handle is null
                || length > MaximumArtifactBytes)
            {
                handle?.Dispose();
                return Task.FromResult<ArtifactContent?>(null);
            }

            var bytes = new byte[length];

            using (handle)
            using (var stream = new FileStream(
                handle,
                FileAccess.Read,
                bufferSize: 81920,
                isAsync: false))
            {
                var offset = 0;

                while (offset < bytes.Length)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    var read = stream.Read(bytes, offset, bytes.Length - offset);

                    if (read == 0)
                    {
                        return Task.FromResult<ArtifactContent?>(null);
                    }

                    offset += read;
                }
            }

            var actual = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

            return Task.FromResult(actual == contentHash.Value
                ? new ArtifactContent(package, contentHash, bytes)
                : null);
        }
        catch (IOException)
        {
            return Task.FromResult<ArtifactContent?>(null);
        }
        catch (UnauthorizedAccessException)
        {
            return Task.FromResult<ArtifactContent?>(null);
        }
        catch (InvalidOperationException)
        {
            return Task.FromResult<ArtifactContent?>(null);
        }
    }

    private string EnsurePackageDirectory(PackageKey package)
    {
        var name = package.Name.Value;
        var version = package.Version.ToString();

        if (!IsSimpleComponent(name) || !IsSimpleComponent(version))
        {
            throw new InvalidOperationException(
                "Package name or version is not safe for filesystem storage.");
        }

        var nameDirectory = EnsureUnderRoot(Path.Combine(_root, name));
        EnsurePrivateDirectory(nameDirectory);
        var versionDirectory = EnsureUnderRoot(Path.Combine(nameDirectory, version));
        EnsurePrivateDirectory(versionDirectory);
        return versionDirectory;
    }

    private string? TryGetPackageDirectory(PackageKey package)
    {
        var name = package.Name.Value;
        var version = package.Version.ToString();

        if (!IsSimpleComponent(name) || !IsSimpleComponent(version))
        {
            return null;
        }

        var nameDirectory = EnsureUnderRoot(Path.Combine(_root, name));

        if (!IsPrivateDirectory(nameDirectory))
        {
            return null;
        }

        var versionDirectory = EnsureUnderRoot(Path.Combine(nameDirectory, version));

        return IsPrivateDirectory(versionDirectory) ? versionDirectory : null;
    }

    private void EnsurePrivateDirectory(string path)
    {
        var creation = UnixFileMetadata.TryCreatePrivateDirectory(path);

        if (creation is not UnixFileMetadata.DirectoryCreationResult.Created
            and not UnixFileMetadata.DirectoryCreationResult.AlreadyExists)
        {
            throw new InvalidOperationException(
                "Artifact package directory could not be created safely.");
        }

        if (!IsPrivateDirectory(path))
        {
            throw new InvalidOperationException(
                "Artifact package directory is not a private service-owned 0700 directory.");
        }
    }

    private void EnsureRootSafe()
    {
        if (!IsSafeRoot(_root))
        {
            throw new InvalidOperationException(
                "Artifact root is no longer a private service-owned 0700 directory.");
        }
    }

    private bool IsSafeRoot(string root) =>
        Directory.Exists(root)
        && !UnixFileMetadata.IsSymbolicLink(root)
        && HasNoSymlinkComponents(root)
        && File.GetUnixFileMode(root) == PrivateDirectoryMode
        && UnixFileMetadata.GetOwnerUid(root) == UnixFileMetadata.GetEffectiveUid();

    private bool IsPrivateDirectory(string path) =>
        Directory.Exists(path)
        && !UnixFileMetadata.IsSymbolicLink(path)
        && File.GetUnixFileMode(path) == PrivateDirectoryMode
        && UnixFileMetadata.GetOwnerUid(path) == UnixFileMetadata.GetEffectiveUid();

    private void EnsureArtifactFileSafe(string path)
    {
        if (!UnixFileMetadata.TryOpenRegularFile(path, out var handle, out _)
            || handle is null)
        {
            throw new InvalidOperationException(
                "Stored artifact is not a regular non-symlink file.");
        }

        handle.Dispose();

        if (File.GetUnixFileMode(path) != PrivateFileMode
            || UnixFileMetadata.GetOwnerUid(path) != UnixFileMetadata.GetEffectiveUid())
        {
            throw new InvalidOperationException(
                "Stored artifact is not a private service-owned 0600 file.");
        }
    }

    private static bool HasNoSymlinkComponents(string path)
    {
        var root = Path.GetPathRoot(path);

        if (string.IsNullOrEmpty(root)
            || !Directory.Exists(root)
            || UnixFileMetadata.IsSymbolicLink(root))
        {
            return false;
        }

        var current = root;
        var relative = Path.GetRelativePath(root, path);

        foreach (var segment in relative.Split(
            Path.DirectorySeparatorChar,
            Path.AltDirectorySeparatorChar))
        {
            if (string.IsNullOrEmpty(segment) || segment == ".")
            {
                continue;
            }

            current = Path.Combine(current, segment);

            if (!Directory.Exists(current) || UnixFileMetadata.IsSymbolicLink(current))
            {
                return false;
            }
        }

        return true;
    }

    private static bool IsSimpleComponent(string value) =>
        !string.IsNullOrWhiteSpace(value)
        && value != "."
        && value != ".."
        && Path.GetFileName(value) == value
        && value.IndexOfAny(Path.GetInvalidFileNameChars()) < 0;

    private string EnsureUnderRoot(string path)
    {
        var fullPath = Path.GetFullPath(path);
        var relative = Path.GetRelativePath(_root, fullPath);

        if (relative == ".."
            || relative.StartsWith($"..{Path.DirectorySeparatorChar}", StringComparison.Ordinal)
            || Path.IsPathRooted(relative))
        {
            throw new InvalidOperationException("Artifact path escapes the configured artifact root.");
        }

        return fullPath;
    }
}
