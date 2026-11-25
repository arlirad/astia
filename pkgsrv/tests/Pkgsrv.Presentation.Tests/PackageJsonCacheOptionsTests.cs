using Microsoft.Extensions.Configuration;
using Pkgsrv.Presentation.Caching;

namespace Pkgsrv.Presentation.Tests;

public sealed class PackageJsonCacheOptionsTests
{
    [Fact]
    public void DefaultsMatchRequiredCacheBehavior()
    {
        var options = Resolve();

        Assert.Equal(TimeSpan.FromSeconds(5), options.Ttl);
        Assert.Equal(1024, options.SizeLimit);
    }

    [Fact]
    public void ReadsConfiguredTtlAndSizeLimit()
    {
        var options = Resolve(
            ("Pkgsrv:Cache:TtlSeconds", "30"),
            ("Pkgsrv:Cache:SizeLimit", "64"));

        Assert.Equal(TimeSpan.FromSeconds(30), options.Ttl);
        Assert.Equal(64, options.SizeLimit);
    }

    [Theory]
    [InlineData("Pkgsrv:Cache:TtlSeconds", "")]
    [InlineData("Pkgsrv:Cache:TtlSeconds", "0")]
    [InlineData("Pkgsrv:Cache:SizeLimit", "")]
    [InlineData("Pkgsrv:Cache:SizeLimit", "0")]
    public void RejectsInvalidConfiguration(string key, string value)
    {
        Assert.Throws<InvalidOperationException>(
            () => Resolve((key, value)));
    }

    private static PackageJsonCacheOptions Resolve(params (string Key, string? Value)[] values) =>
        PackageJsonCacheOptions.FromConfiguration(
            new ConfigurationBuilder()
                .AddInMemoryCollection(values.Select(value =>
                    new KeyValuePair<string, string?>(value.Key, value.Value)))
                .Build());
}
