namespace Pkgsrv.Application.Caching;

public sealed record PackageReadModelCacheOptions
{
    public static PackageReadModelCacheOptions Default { get; } = new(
        maxEntries: 1024,
        absoluteExpiration: TimeSpan.FromMinutes(5),
        slidingExpiration: TimeSpan.FromMinutes(1));

    public PackageReadModelCacheOptions(
        int maxEntries,
        TimeSpan absoluteExpiration,
        TimeSpan slidingExpiration)
    {
        if (maxEntries <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(maxEntries),
                "The read-model cache size must be greater than zero.");
        }

        if (absoluteExpiration <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(
                nameof(absoluteExpiration),
                "The read-model cache absolute expiration must be greater than zero.");
        }

        if (slidingExpiration <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(
                nameof(slidingExpiration),
                "The read-model cache sliding expiration must be greater than zero.");
        }

        MaxEntries = maxEntries;
        AbsoluteExpiration = absoluteExpiration;
        SlidingExpiration = slidingExpiration;
    }

    public int MaxEntries { get; }

    public TimeSpan AbsoluteExpiration { get; }

    public TimeSpan SlidingExpiration { get; }
}
