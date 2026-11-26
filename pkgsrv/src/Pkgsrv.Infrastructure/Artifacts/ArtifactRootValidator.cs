#pragma warning disable CA1416
using Pkgsrv.Infrastructure.Builds;

namespace Pkgsrv.Infrastructure.Artifacts;

public static class ArtifactRootValidator
{
    private const UnixFileMode PrivateDirectoryMode =
        UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute;
    internal const long RequiredAvailableInodes = 3;

    public static bool TryValidate(
        string root,
        long maxBytes,
        long maxInodes,
        long requiredAvailableBytes,
        out string detail)
    {
        detail = string.Empty;

        if (!OperatingSystem.IsLinux())
        {
            detail = "Durable artifact storage requires a Linux service account.";
            return false;
        }

        if (string.IsNullOrWhiteSpace(root)
            || !Path.IsPathFullyQualified(root)
            || !Directory.Exists(root))
        {
            detail = "Artifact root must be a pre-provisioned absolute directory.";
            return false;
        }

        var fullRoot = Path.GetFullPath(root);

        if (UnixFileMetadata.IsSymbolicLink(fullRoot)
            || !HasNoSymlinkComponents(fullRoot))
        {
            detail = "Artifact root must not contain symlink components.";
            return false;
        }

        if (File.GetUnixFileMode(fullRoot) != PrivateDirectoryMode
            || UnixFileMetadata.GetOwnerUid(fullRoot) != UnixFileMetadata.GetEffectiveUid())
        {
            detail = "Artifact root must be service-owned with mode exactly 0700.";
            return false;
        }

        if (maxBytes <= 0 || maxInodes <= 0 || requiredAvailableBytes <= 0)
        {
            detail = "Pkgsrv:Artifacts:QuotaBytes, Pkgsrv:Artifacts:QuotaInodes, and a positive required artifact capacity must be configured.";
            return false;
        }

        if (!WorkspaceFilesystemQuota.TryReadDedicatedMount(
                fullRoot,
                out var mount,
                out _,
                out var totalBytes,
                out var totalInodes,
                out var availableBytes,
                out var availableInodes)
            || !MeetsCaps(
                mount.FileSystemType,
                totalBytes,
                totalInodes,
                maxBytes,
                maxInodes)
            || !MeetsAvailableCapacity(
                availableBytes,
                availableInodes,
                requiredAvailableBytes,
                RequiredAvailableInodes))
        {
            detail = "Artifact root must be a separate operator-provisioned persistent filesystem/device mounted at the exact artifact root, with enough free bytes and inodes; QuotaBytes/QuotaInodes are upper-bound validation ceilings, not quotas. Bind-mounted subdirectories, shared devices, tmpfs, and ramfs are not accepted.";
            return false;
        }

        return true;
    }

    internal static bool MeetsCaps(
        string fileSystemType,
        UInt128 totalBytes,
        UInt128 totalInodes,
        long maxBytes,
        long maxInodes) =>
        WorkspaceFilesystemQuota.MeetsDurableCaps(
            fileSystemType,
            totalBytes,
            totalInodes,
            maxBytes,
            maxInodes);

    internal static bool MeetsAvailableCapacity(
        UInt128 availableBytes,
        UInt128 availableInodes,
        long requiredBytes,
        long requiredInodes) =>
        requiredBytes > 0
        && requiredInodes > 0
        && availableBytes >= (UInt128)requiredBytes
        && availableInodes >= (UInt128)requiredInodes;

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
}
