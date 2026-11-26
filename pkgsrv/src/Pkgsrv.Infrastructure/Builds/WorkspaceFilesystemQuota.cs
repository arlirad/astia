using System.Runtime.InteropServices;

namespace Pkgsrv.Infrastructure.Builds;

internal sealed record LinuxMountEntry(
    int Id,
    int ParentId,
    string Device,
    string Root,
    string MountPoint,
    string FileSystemType,
    string Source);

internal static class WorkspaceFilesystemQuota
{
    private const string Libc = "libc";

    public static bool IsCapped(string root, long maxBytes, long maxInodes)
    {
        if (!OperatingSystem.IsLinux() || maxBytes <= 0 || maxInodes <= 0)
        {
            return false;
        }

        try
        {
            return TryReadDedicatedMount(
                    root,
                    out var mount,
                    out _,
                    out var totalBytes,
                    out var totalInodes,
                    out _,
                    out _)
                && MeetsCaps(mount.FileSystemType, totalBytes, totalInodes, maxBytes, maxInodes);
        }
        catch (IOException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
        catch (DllNotFoundException)
        {
            return false;
        }
        catch (EntryPointNotFoundException)
        {
            return false;
        }
    }

    internal static bool TryReadDedicatedMount(
        string root,
        out LinuxMountEntry mount,
        out LinuxMountEntry parent,
        out UInt128 totalBytes,
        out UInt128 totalInodes,
        out UInt128 availableBytes,
        out UInt128 availableInodes)
    {
        mount = null!;
        parent = null!;
        totalBytes = 0;
        totalInodes = 0;
        availableBytes = 0;
        availableInodes = 0;
        var fullRoot = NormalizePath(root);

        if (!TryFindMount(fullRoot, out mount)
            || !IsDedicatedMount(fullRoot, mount, out parent)
            || statvfs(fullRoot, out var status) != 0
            || status.FragmentSize == 0
            || status.Blocks == 0)
        {
            return false;
        }

        totalBytes = (UInt128)status.Blocks * status.FragmentSize;
        totalInodes = status.Files;
        availableBytes = (UInt128)status.BlocksAvailable * status.FragmentSize;
        availableInodes = status.FilesAvailable;
        return true;
    }

    internal static bool IsDedicatedMount(
        string expectedRoot,
        LinuxMountEntry mount,
        out LinuxMountEntry parent)
    {
        parent = null!;

        return TryFindParent(mount, out parent)
            && IsDedicatedMountIdentity(expectedRoot, mount, parent);
    }

    internal static bool IsDedicatedMountIdentity(
        string expectedRoot,
        LinuxMountEntry mount,
        LinuxMountEntry? parent)
    {
        if (parent is null
            || string.IsNullOrWhiteSpace(mount.Device)
            || string.IsNullOrWhiteSpace(mount.Source)
            || string.IsNullOrWhiteSpace(mount.MountPoint)
            || string.IsNullOrWhiteSpace(mount.FileSystemType)
            || !string.Equals(mount.Root, "/", StringComparison.Ordinal)
            || !string.Equals(
                NormalizePath(mount.MountPoint),
                NormalizePath(expectedRoot),
                StringComparison.Ordinal))
        {
            return false;
        }

        return !string.Equals(parent.Device, mount.Device, StringComparison.Ordinal);
    }

    internal static bool MeetsDurableCaps(
        string fileSystemType,
        UInt128 totalBytes,
        UInt128 totalInodes,
        long maxBytes,
        long maxInodes)
    {
        if (IsVolatileFileSystem(fileSystemType)
            || maxBytes <= 0
            || maxInodes <= 0
            || totalBytes == 0
            || totalInodes == 0)
        {
            return false;
        }

        return totalBytes <= (UInt128)maxBytes
            && totalInodes <= (UInt128)maxInodes;
    }

    private static bool IsVolatileFileSystem(string fileSystemType) =>
        fileSystemType is "tmpfs"
            or "ramfs"
            or "devtmpfs"
            or "proc"
            or "sysfs"
            or "cgroup"
            or "cgroup2"
            or "debugfs"
            or "securityfs"
            or "tracefs"
            or "pstore"
            or "bpf"
            or "autofs"
            or "overlay";

    internal static bool MeetsCaps(
        string fileSystemType,
        UInt128 totalBytes,
        UInt128 totalInodes,
        long maxBytes,
        long maxInodes)
    {
        if (!string.Equals(fileSystemType, "tmpfs", StringComparison.Ordinal)
            || maxBytes <= 0
            || maxInodes <= 0
            || totalBytes == 0
            || totalInodes == 0)
        {
            return false;
        }

        return totalBytes <= (UInt128)maxBytes
            && totalInodes <= (UInt128)maxInodes;
    }

    private static bool TryFindMount(string path, out LinuxMountEntry entry)
    {
        entry = null!;
        var bestLength = -1;

        foreach (var line in File.ReadLines("/proc/self/mountinfo"))
        {
            if (!TryParseLine(line, out var candidate))
            {
                return false;
            }

            if (!IsWithin(path, candidate.MountPoint)
                || candidate.MountPoint.Length <= bestLength)
            {
                continue;
            }

            bestLength = candidate.MountPoint.Length;
            entry = candidate;
        }

        return bestLength >= 0;
    }

    private static bool TryFindParent(
        LinuxMountEntry mount,
        out LinuxMountEntry parent)
    {
        parent = null!;

        foreach (var line in File.ReadLines("/proc/self/mountinfo"))
        {
            if (!TryParseLine(line, out var candidate))
            {
                return false;
            }

            if (candidate.Id == mount.ParentId)
            {
                parent = candidate;
                return true;
            }
        }

        return false;
    }

    private static bool TryParseLine(string line, out LinuxMountEntry entry)
    {
        entry = null!;
        var fields = line.Split(' ');

        if (fields.Length < 7
            || !int.TryParse(fields[0], out var id)
            || !int.TryParse(fields[1], out var parentId))
        {
            return false;
        }

        var separator = Array.IndexOf(fields, "-");

        if (separator < 0 || separator + 2 >= fields.Length)
        {
            return false;
        }

        var device = fields[2];
        var root = Unescape(fields[3]);
        var mountPoint = NormalizePath(Unescape(fields[4]));
        var fileSystemType = fields[separator + 1];
        var source = Unescape(fields[separator + 2]);

        if (string.IsNullOrWhiteSpace(device)
            || string.IsNullOrWhiteSpace(root)
            || string.IsNullOrWhiteSpace(mountPoint)
            || string.IsNullOrWhiteSpace(fileSystemType)
            || string.IsNullOrWhiteSpace(source))
        {
            return false;
        }

        entry = new LinuxMountEntry(
            id,
            parentId,
            device,
            root,
            mountPoint,
            fileSystemType,
            source);
        return true;
    }

    private static bool IsWithin(string path, string candidate) =>
        string.Equals(path, candidate, StringComparison.Ordinal)
        || path.StartsWith($"{candidate}/", StringComparison.Ordinal);

    internal static string NormalizePath(string path)
    {
        var full = Path.GetFullPath(path);

        return full.Length > 1
            ? full.TrimEnd(Path.DirectorySeparatorChar)
            : full;
    }

    private static string Unescape(string value) =>
        value
            .Replace("\\040", " ", StringComparison.Ordinal)
            .Replace("\\011", "\t", StringComparison.Ordinal)
            .Replace("\\012", "\n", StringComparison.Ordinal)
            .Replace("\\134", "\\", StringComparison.Ordinal);

    [DllImport(Libc, EntryPoint = "statvfs", CharSet = CharSet.Ansi, SetLastError = true)]
    private static extern int statvfs(string path, out StatVfs buffer);

    [StructLayout(LayoutKind.Sequential)]
    private struct StatVfs
    {
        public ulong BlockSize;
        public ulong FragmentSize;
        public ulong Blocks;
        public ulong BlocksFree;
        public ulong BlocksAvailable;
        public ulong Files;
        public ulong FilesFree;
        public ulong FilesAvailable;
        public ulong FileSystemId;
        public ulong Flags;
        public ulong NameMax;
        public int Spare0;
        public int Spare1;
        public int Spare2;
        public int Spare3;
        public int Spare4;
        public int Spare5;
    }
}
