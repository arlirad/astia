using System.Globalization;
using Microsoft.Extensions.Configuration;
using Pkgsrv.Application.Caching;
using Pkgsrv.Infrastructure.Postgres;

namespace Pkgsrv.Presentation.Configuration;

public enum PkgsrvStorageProvider
{
    InMemory = 0,
    Postgres = 1
}

public sealed record PkgsrvStorageOptions
{
    public const string StorageSection = "Pkgsrv:Storage";
    public const string CacheSection = "Pkgsrv:Cache";
    public const int DefaultMaxCacheEntries = 1024;
    public const int DefaultAbsoluteExpirationSeconds = 300;
    public const int DefaultSlidingExpirationSeconds = 60;
    public const int MaximumExpirationSeconds = 86400;

    public static PkgsrvStorageOptions DefaultInMemory { get; } = new(
        PkgsrvStorageProvider.InMemory,
        null,
        PostgresPackageStoreOptions.DefaultMaxListResults,
        PackageReadModelCacheOptions.Default);

    public PkgsrvStorageOptions(
        PkgsrvStorageProvider provider,
        string? postgresConnectionString,
        int maxListResults,
        PackageReadModelCacheOptions cache)
    {
        if (!Enum.IsDefined(provider))
        {
            throw new ArgumentOutOfRangeException(nameof(provider), "The package storage provider is invalid.");
        }

        if (maxListResults <= 0 || maxListResults > PostgresPackageStoreOptions.MaximumListResults)
        {
            throw new ArgumentOutOfRangeException(
                nameof(maxListResults),
                $"The maximum list result count must be between 1 and {PostgresPackageStoreOptions.MaximumListResults}.");
        }

        if (provider == PkgsrvStorageProvider.Postgres
            && string.IsNullOrWhiteSpace(postgresConnectionString))
        {
            throw new ArgumentException(
                "A PostgreSQL connection string is required when the PostgreSQL provider is selected.",
                nameof(postgresConnectionString));
        }

        ArgumentNullException.ThrowIfNull(cache);

        Provider = provider;
        PostgresConnectionString = postgresConnectionString;
        MaxListResults = maxListResults;
        Cache = cache;
    }

    public PkgsrvStorageProvider Provider { get; }

    public string? PostgresConnectionString { get; }

    public int MaxListResults { get; }

    public PackageReadModelCacheOptions Cache { get; }

    public static PkgsrvStorageOptions Postgres(
        string connectionString,
        int maxListResults = PostgresPackageStoreOptions.DefaultMaxListResults,
        PackageReadModelCacheOptions? cache = null) =>
        new(
            PkgsrvStorageProvider.Postgres,
            connectionString,
            maxListResults,
            cache ?? PackageReadModelCacheOptions.Default);

    public static PkgsrvStorageOptions FromConfiguration(IConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(configuration);

        var storageSection = configuration.GetSection(StorageSection);
        var providerValue = ReadOptional(storageSection, "Provider", $"{StorageSection}:Provider");
        var connectionString = ReadOptional(
            storageSection,
            "PostgresConnectionString",
            $"{StorageSection}:PostgresConnectionString");
        var maxListResults = ReadInt(
            storageSection,
            "MaxListResults",
            $"{StorageSection}:MaxListResults",
            PostgresPackageStoreOptions.DefaultMaxListResults,
            1,
            PostgresPackageStoreOptions.MaximumListResults);

        var provider = ResolveProvider(providerValue, connectionString);

        if (provider == PkgsrvStorageProvider.Postgres
            && string.IsNullOrWhiteSpace(connectionString))
        {
            throw new InvalidOperationException(
                $"{StorageSection}:PostgresConnectionString is required when the PostgreSQL provider is selected.");
        }

        var cacheSection = configuration.GetSection(CacheSection);
        var cache = new PackageReadModelCacheOptions(
            ReadInt(
                cacheSection,
                "MaxEntries",
                $"{CacheSection}:MaxEntries",
                DefaultMaxCacheEntries,
                1,
                int.MaxValue),
            TimeSpan.FromSeconds(ReadInt(
                cacheSection,
                "AbsoluteExpirationSeconds",
                $"{CacheSection}:AbsoluteExpirationSeconds",
                DefaultAbsoluteExpirationSeconds,
                1,
                MaximumExpirationSeconds)),
            TimeSpan.FromSeconds(ReadInt(
                cacheSection,
                "SlidingExpirationSeconds",
                $"{CacheSection}:SlidingExpirationSeconds",
                DefaultSlidingExpirationSeconds,
                1,
                MaximumExpirationSeconds)));

        return new PkgsrvStorageOptions(provider, connectionString, maxListResults, cache);
    }

    private static PkgsrvStorageProvider ResolveProvider(
        string? providerValue,
        string? connectionString)
    {
        if (providerValue is null)
        {
            return string.IsNullOrWhiteSpace(connectionString)
                ? PkgsrvStorageProvider.InMemory
                : PkgsrvStorageProvider.Postgres;
        }

        if (!Enum.TryParse<PkgsrvStorageProvider>(providerValue, ignoreCase: true, out var provider)
            || !Enum.IsDefined(provider))
        {
            throw new InvalidOperationException(
                $"{StorageSection}:Provider must be InMemory or Postgres when supplied.");
        }

        return provider;
    }

    private static string? ReadOptional(
        IConfigurationSection section,
        string relativeKey,
        string fullKey)
    {
        var value = section[relativeKey];

        if (value is null)
        {
            return null;
        }

        if (string.IsNullOrWhiteSpace(value))
        {
            throw new InvalidOperationException(
                $"{fullKey} was supplied but is empty. Omit the key to use the default.");
        }

        return value.Trim();
    }

    private static int ReadInt(
        IConfigurationSection section,
        string relativeKey,
        string fullKey,
        int defaultValue,
        int minimum,
        int maximum)
    {
        var value = ReadOptional(section, relativeKey, fullKey);

        if (value is null)
        {
            return defaultValue;
        }

        if (!int.TryParse(
                value,
                NumberStyles.Integer,
                CultureInfo.InvariantCulture,
                out var parsed)
            || parsed < minimum
            || parsed > maximum)
        {
            throw new InvalidOperationException(
                $"{fullKey} must be an integer between {minimum} and {maximum}.");
        }

        return parsed;
    }
}
