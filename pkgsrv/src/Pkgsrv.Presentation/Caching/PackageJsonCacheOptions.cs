using Microsoft.Extensions.Configuration;

namespace Pkgsrv.Presentation.Caching;

public sealed record PackageJsonCacheOptions
{
    public const int DefaultTtlSeconds = 5;
    public const long DefaultSizeLimit = 1024;

    private PackageJsonCacheOptions(TimeSpan ttl, long sizeLimit)
    {
        Ttl = ttl;
        SizeLimit = sizeLimit;
    }

    public TimeSpan Ttl { get; }

    public long SizeLimit { get; }

    public static PackageJsonCacheOptions Default { get; } =
        new(TimeSpan.FromSeconds(DefaultTtlSeconds), DefaultSizeLimit);

    public static PackageJsonCacheOptions Create(TimeSpan ttl, long sizeLimit)
    {
        if (ttl <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(ttl), "Cache TTL must be positive.");
        }

        if (sizeLimit <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(sizeLimit), "Cache size limit must be positive.");
        }

        return new PackageJsonCacheOptions(ttl, sizeLimit);
    }

    public static PackageJsonCacheOptions FromConfiguration(IConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(configuration);

        var section = configuration.GetSection("Pkgsrv:Cache");
        var ttlRaw = section["TtlSeconds"];
        var ttlSeconds = DefaultTtlSeconds;

        if (ttlRaw is not null
            && (string.IsNullOrWhiteSpace(ttlRaw)
                || !int.TryParse(ttlRaw, out ttlSeconds)
                || ttlSeconds <= 0))
        {
            throw new InvalidOperationException(
                "Pkgsrv:Cache:TtlSeconds must be a positive integer when supplied.");
        }

        var sizeLimitRaw = section["SizeLimit"];
        var sizeLimit = DefaultSizeLimit;

        if (sizeLimitRaw is not null
            && (string.IsNullOrWhiteSpace(sizeLimitRaw)
                || !long.TryParse(sizeLimitRaw, out sizeLimit)
                || sizeLimit <= 0))
        {
            throw new InvalidOperationException(
                "Pkgsrv:Cache:SizeLimit must be a positive integer when supplied.");
        }

        return new PackageJsonCacheOptions(TimeSpan.FromSeconds(ttlSeconds), sizeLimit);
    }
}
