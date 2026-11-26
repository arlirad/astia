using System.Text;
using Pkgsrv.Infrastructure.Builds;

namespace Pkgsrv.Infrastructure.Artifacts;

internal static class RsaKeyFiles
{
    private const long MaximumKeyBytes = 64 * 1024;
    private const UnixFileMode PrivateKeyMode = UnixFileMode.UserRead;
    private const UnixFileMode PrivateKeyWriteMode =
        UnixFileMode.UserRead | UnixFileMode.UserWrite;

    public static string ReadPrivateKey(string path) =>
        ReadPrivateKey(path, UnixFileMetadata.GetEffectiveUid());

    internal static string ReadPrivateKey(string path, uint expectedOwnerUid) =>
        ReadKey(path, expectedOwnerUid, requirePrivateMetadata: true);

    public static string ReadPublicKey(string path) =>
        ReadKey(path, expectedOwnerUid: null, requirePrivateMetadata: false);

    private static string ReadKey(
        string path,
        uint? expectedOwnerUid,
        bool requirePrivateMetadata)
    {
        if (string.IsNullOrWhiteSpace(path)
            || !OperatingSystem.IsLinux()
            || !UnixFileMetadata.TryOpenRegularFile(
                path,
                out var handle,
                out var length,
                out var ownerUid,
                out var mode)
            || handle is null)
        {
            throw new InvalidOperationException(
                "Configured key file must be a regular non-symlink file.");
        }

        if (length <= 0 || length > MaximumKeyBytes)
        {
            handle.Dispose();
            throw new InvalidOperationException(
                "Configured key file has an invalid size.");
        }

        if (expectedOwnerUid is { } expectedUid && ownerUid != expectedUid)
        {
            handle.Dispose();
            throw new InvalidOperationException(
                "Configured private key must be owned by the service account.");
        }

        if (requirePrivateMetadata
            && mode != PrivateKeyMode
            && mode != PrivateKeyWriteMode)
        {
            handle.Dispose();
            throw new InvalidOperationException(
                "Configured private key must be owner-only mode 0400 or 0600.");
        }

        try
        {
            using (handle)
            using (var stream = new FileStream(
                handle,
                FileAccess.Read,
                bufferSize: 4096,
                isAsync: false))
            {
                var bytes = new byte[length];
                var offset = 0;

                while (offset < bytes.Length)
                {
                    var read = stream.Read(bytes, offset, bytes.Length - offset);

                    if (read == 0)
                    {
                        throw new InvalidOperationException(
                            "Configured key file changed size while being read.");
                    }

                    offset += read;
                }

                return Encoding.UTF8.GetString(bytes);
            }
        }
        catch (IOException)
        {
            throw new InvalidOperationException(
                "Configured key file could not be read.");
        }
    }
}
