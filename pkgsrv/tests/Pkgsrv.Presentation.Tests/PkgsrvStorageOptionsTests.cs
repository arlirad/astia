using Microsoft.Extensions.Configuration;
using Pkgsrv.Presentation.Configuration;

namespace Pkgsrv.Presentation.Tests;

public sealed class PkgsrvStorageOptionsTests
{
    [Fact]
    public void FromConfiguration_DefaultsToInMemoryWithoutDatabaseConfiguration()
    {
        var options = PkgsrvStorageOptions.FromConfiguration(Configuration());

        Assert.Equal(PkgsrvStorageProvider.InMemory, options.Provider);
        Assert.Null(options.PostgresConnectionString);
        Assert.Equal(1000, options.MaxListResults);
        Assert.Equal(PkgsrvStorageOptions.DefaultMaxCacheEntries, options.Cache.MaxEntries);
    }

    [Fact]
    public void FromConfiguration_SelectsPostgresWhenConnectionStringIsSupplied()
    {
        var options = PkgsrvStorageOptions.FromConfiguration(Configuration(
            ("Pkgsrv:Storage:PostgresConnectionString", "Host=localhost;Database=pkgsrv")));

        Assert.Equal(PkgsrvStorageProvider.Postgres, options.Provider);
        Assert.Equal("Host=localhost;Database=pkgsrv", options.PostgresConnectionString);
    }

    [Fact]
    public void FromConfiguration_AllowsExplicitInMemoryEvenWithConnectionString()
    {
        var options = PkgsrvStorageOptions.FromConfiguration(Configuration(
            ("Pkgsrv:Storage:Provider", "InMemory"),
            ("Pkgsrv:Storage:PostgresConnectionString", "Host=localhost;Database=pkgsrv")));

        Assert.Equal(PkgsrvStorageProvider.InMemory, options.Provider);
    }

    [Fact]
    public void FromConfiguration_RejectsPostgresWithoutConnectionString()
    {
        var exception = Assert.Throws<InvalidOperationException>(
            () => PkgsrvStorageOptions.FromConfiguration(Configuration(
                ("Pkgsrv:Storage:Provider", "Postgres"))));

        Assert.Contains("PostgresConnectionString", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void FromConfiguration_RejectsUnknownProvider()
    {
        Assert.Throws<InvalidOperationException>(
            () => PkgsrvStorageOptions.FromConfiguration(Configuration(
                ("Pkgsrv:Storage:Provider", "Sqlite"))));
    }

    [Fact]
    public void FromConfiguration_ParsesCacheAndListBounds()
    {
        var options = PkgsrvStorageOptions.FromConfiguration(Configuration(
            ("Pkgsrv:Storage:MaxListResults", "25"),
            ("Pkgsrv:Cache:MaxEntries", "16"),
            ("Pkgsrv:Cache:AbsoluteExpirationSeconds", "120"),
            ("Pkgsrv:Cache:SlidingExpirationSeconds", "30")));

        Assert.Equal(25, options.MaxListResults);
        Assert.Equal(16, options.Cache.MaxEntries);
        Assert.Equal(TimeSpan.FromSeconds(120), options.Cache.AbsoluteExpiration);
        Assert.Equal(TimeSpan.FromSeconds(30), options.Cache.SlidingExpiration);
    }

    [Fact]
    public void FromConfiguration_RejectsEmptyConnectionString()
    {
        Assert.Throws<InvalidOperationException>(
            () => PkgsrvStorageOptions.FromConfiguration(Configuration(
                ("Pkgsrv:Storage:PostgresConnectionString", "   "))));
    }

    [Fact]
    public void FromConfiguration_RejectsOutOfRangeListLimit()
    {
        Assert.Throws<InvalidOperationException>(
            () => PkgsrvStorageOptions.FromConfiguration(Configuration(
                ("Pkgsrv:Storage:MaxListResults", "0"))));
    }

    private static IConfiguration Configuration(params (string Key, string? Value)[] values) =>
        new ConfigurationBuilder()
            .AddInMemoryCollection(values.Select(value =>
                new KeyValuePair<string, string?>(value.Key, value.Value)))
            .Build();
}
