using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Pkgsrv.Infrastructure.Builds;

internal static class UnixFileMetadata
{
    public const int EExist = 17;
    private const string Libc = "libc";
    private const int OReadOnly = 0;
    private const int ONonBlock = 0x800;
    private const int ONoFollow = 0x20000;
    private const int OCloseOnExec = 0x80000;
    private const uint FileTypeMask = 0xF000;
    private const uint RegularFileType = 0x8000;
    private const uint SymbolicLinkType = 0xA000;

    public enum DirectoryCreationResult
    {
        Created,
        AlreadyExists,
        Failed,
        Unsupported
    }

    public static DirectoryCreationResult TryCreatePrivateDirectory(
        string path,
        UnixFileMode mode = UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute)
    {
        if (!OperatingSystem.IsLinux())
        {
            return DirectoryCreationResult.Unsupported;
        }

        try
        {
            if (mkdir(path, (uint)mode) == 0)
            {
                return DirectoryCreationResult.Created;
            }

            return Marshal.GetLastPInvokeError() == EExist
                ? DirectoryCreationResult.AlreadyExists
                : DirectoryCreationResult.Failed;
        }
        catch (DllNotFoundException)
        {
            return DirectoryCreationResult.Unsupported;
        }
        catch (EntryPointNotFoundException)
        {
            return DirectoryCreationResult.Unsupported;
        }
    }

    public static uint GetEffectiveUid()
    {
        if (!OperatingSystem.IsLinux())
        {
            return uint.MaxValue;
        }

        try
        {
            return geteuid();
        }
        catch (DllNotFoundException)
        {
            return uint.MaxValue;
        }
        catch (EntryPointNotFoundException)
        {
            return uint.MaxValue;
        }
    }

    public static uint GetOwnerUid(string path)
    {
        if (!OperatingSystem.IsLinux())
        {
            return uint.MaxValue;
        }

        try
        {
            return lstat(path, out var status) == 0 ? status.StUid : uint.MaxValue;
        }
        catch (DllNotFoundException)
        {
            return uint.MaxValue;
        }
        catch (EntryPointNotFoundException)
        {
            return uint.MaxValue;
        }
    }

    public static bool IsSymbolicLink(string path)
    {
        if (!OperatingSystem.IsLinux())
        {
            return false;
        }

        try
        {
            return lstat(path, out var status) == 0
                && (status.StMode & FileTypeMask) == SymbolicLinkType;
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

    public static bool TryOpenRegularFile(
        string path,
        out SafeFileHandle? handle,
        out long length) =>
        TryOpenRegularFile(path, out handle, out length, out _, out _);

    public static bool TryOpenRegularFile(
        string path,
        out SafeFileHandle? handle,
        out long length,
        out uint ownerUid,
        out UnixFileMode mode)
    {
        handle = null;
        length = 0;
        ownerUid = uint.MaxValue;
        mode = 0;

        if (!OperatingSystem.IsLinux())
        {
            return false;
        }

        try
        {
            var descriptor = open(
                path,
                OReadOnly | ONonBlock | ONoFollow | OCloseOnExec);

            if (descriptor < 0)
            {
                return false;
            }

            var safeHandle = new SafeFileHandle((IntPtr)descriptor, ownsHandle: true);

            if (fstat(descriptor, out var status) != 0
                || (status.StMode & FileTypeMask) != RegularFileType
                || status.StSize < 0)
            {
                safeHandle.Dispose();
                return false;
            }

            handle = safeHandle;
            length = status.StSize;
            ownerUid = status.StUid;
            mode = (UnixFileMode)(status.StMode & 0xFFF);
            return true;
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

    [DllImport(Libc, EntryPoint = "geteuid")]
    private static extern uint geteuid();

    [DllImport(Libc, EntryPoint = "mkdir", CharSet = CharSet.Ansi, SetLastError = true)]
    private static extern int mkdir(string path, uint mode);

    [DllImport(Libc, EntryPoint = "lstat", CharSet = CharSet.Ansi, SetLastError = true)]
    private static extern int lstat(string path, out StatBuffer buffer);

    [DllImport(Libc, EntryPoint = "open", CharSet = CharSet.Ansi, SetLastError = true)]
    private static extern int open(string path, int flags);

    [DllImport(Libc, EntryPoint = "fstat", SetLastError = true)]
    private static extern int fstat(int descriptor, out StatBuffer buffer);

    [StructLayout(LayoutKind.Sequential)]
    private struct StatBuffer
    {
        public ulong StDev;
        public ulong StIno;
        public ulong StNlink;
        public uint StMode;
        public uint StUid;
        public uint StGid;
        public int Pad0;
        public ulong StRdev;
        public long StSize;
        public long StBlksize;
        public long StBlocks;
        public long StAtimeSec;
        public long StAtimeNsec;
        public long StMtimeSec;
        public long StMtimeNsec;
        public long StCtimeSec;
        public long StCtimeNsec;
        public long Reserved0;
        public long Reserved1;
        public long Reserved2;
    }
}
