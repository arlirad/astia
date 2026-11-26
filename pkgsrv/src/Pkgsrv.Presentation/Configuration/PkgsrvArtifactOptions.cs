using Microsoft.Extensions.Configuration;

namespace Pkgsrv.Presentation.Configuration;

public sealed record PkgsrvArtifactOptions(
    string? Root,
    long QuotaBytes,
    long QuotaInodes)
{
    public const long MaximumQuotaBytes = 1024L * 1024 * 1024 * 1024;
    public const long MaximumQuotaInodes = 100_000_000;

    public static PkgsrvArtifactOptions FromConfiguration(IConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(configuration);

        var section = configuration.GetSection("Pkgsrv:Artifacts");
        var root = section["Root"]?.Trim();
        var quotaBytes = ReadQuota(section["QuotaBytes"], "Pkgsrv:Artifacts:QuotaBytes", MaximumQuotaBytes);
        var quotaInodes = ReadQuota(section["QuotaInodes"], "Pkgsrv:Artifacts:QuotaInodes", MaximumQuotaInodes);

        if (string.IsNullOrWhiteSpace(root))
        {
            return new PkgsrvArtifactOptions(Root: null, quotaBytes, quotaInodes);
        }

        if (!Path.IsPathFullyQualified(root))
        {
            throw new InvalidOperationException("Pkgsrv:Artifacts:Root must be an absolute path.");
        }

        return new PkgsrvArtifactOptions(root, quotaBytes, quotaInodes);
    }

    private static long ReadQuota(string? value, string key, long maximum)
    {
        if (value is null)
        {
            return 0;
        }

        if (string.IsNullOrWhiteSpace(value)
            || !long.TryParse(value, out var parsed)
            || parsed <= 0
            || parsed > maximum)
        {
            throw new InvalidOperationException(
                $"{key} must be between 1 and {maximum}.");
        }

        return parsed;
    }
}

public sealed record PkgsrvSigningOptions(
    string? PrivateKeyPath,
    string? PublicKeyPath)
{
    public bool CanSign => !string.IsNullOrWhiteSpace(PrivateKeyPath);

    public static PkgsrvSigningOptions FromConfiguration(IConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(configuration);

        var section = configuration.GetSection("Pkgsrv:Signing");
        var privateKeyPath = ReadOptionalPath(section["PrivateKeyPath"], "Pkgsrv:Signing:PrivateKeyPath");
        var publicKeyPath = ReadOptionalPath(section["PublicKeyPath"], "Pkgsrv:Signing:PublicKeyPath");

        return new PkgsrvSigningOptions(privateKeyPath, publicKeyPath);
    }

    private static string? ReadOptionalPath(string? value, string key)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return null;
        }

        var path = value.Trim();

        if (!Path.IsPathFullyQualified(path))
        {
            throw new InvalidOperationException($"{key} must be an absolute path.");
        }

        return path;
    }
}
