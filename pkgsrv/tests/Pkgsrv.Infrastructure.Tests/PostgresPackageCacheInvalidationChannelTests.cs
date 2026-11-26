using Pkgsrv.Application.Interfaces.Caching;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Caching.Postgres;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPackageCacheInvalidationChannelTests
{
    [Fact]
    public void ChannelIsFixedAndQuotedForListen()
    {
        Assert.Equal("pkgsrv_package_cache_invalidation", PostgresPackageCacheInvalidationChannel.ChannelName);
        Assert.Equal(
            "LISTEN \"pkgsrv_package_cache_invalidation\"",
            PostgresPackageCacheInvalidationChannel.ListenCommand);
        Assert.Equal(
            "SELECT pg_notify(@channel, @payload)",
            PostgresPackageCacheInvalidationChannel.NotifyCommand);
    }

    [Fact]
    public void AllAndListPayloads_RoundTrip()
    {
        Assert.True(PostgresPackageCacheInvalidationChannel.TryParse(
            PostgresPackageCacheInvalidationChannel.Serialize(PackageCacheInvalidation.All),
            out var all));
        Assert.Equal(PackageCacheInvalidationScope.All, all.Scope);
        Assert.Null(all.Package);

        Assert.True(PostgresPackageCacheInvalidationChannel.TryParse(
            PostgresPackageCacheInvalidationChannel.Serialize(PackageCacheInvalidation.List),
            out var list));
        Assert.Equal(PackageCacheInvalidationScope.List, list.Scope);
        Assert.Null(list.Package);
    }

    [Fact]
    public void PackagePayload_RoundTripsWithoutSecretsOrTokens()
    {
        var package = PackageKey.Create("widget", "1.2.3-rc.1+build.7");
        var payload = PostgresPackageCacheInvalidationChannel.Serialize(
            PackageCacheInvalidation.ForPackage(package));

        Assert.True(PostgresPackageCacheInvalidationChannel.TryParse(
            payload,
            out var parsed));
        Assert.Equal(PackageCacheInvalidationScope.Package, parsed.Scope);
        Assert.Equal(package, parsed.Package);
        Assert.DoesNotContain("Host=", payload, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("Password", payload, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("token", payload, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("secret", payload, StringComparison.OrdinalIgnoreCase);
    }

    [Theory]
    [InlineData("")]
    [InlineData("not-json")]
    [InlineData("{}")]
    [InlineData("{\"scope\":\"unknown\"}")]
    [InlineData("{\"scope\":\"package\"}")]
    [InlineData("{\"scope\":\"package\",\"name\":\"widget\"}")]
    [InlineData("{\"scope\":\"package\",\"version\":\"1.2.3\"}")]
    [InlineData("{\"scope\":\"package\",\"name\":\"Bad Name\",\"version\":\"1.2.3\"}")]
    [InlineData("{\"scope\":\"package\",\"name\":\"widget\",\"version\":\"1\"}")]
    [InlineData("[]")]
    public void MalformedPayloads_AreRejected(string payload)
    {
        Assert.False(PostgresPackageCacheInvalidationChannel.TryParse(
            payload,
            out _));
    }

    [Fact]
    public void OversizedPayload_IsRejected()
    {
        var payload = new string(
            'a',
            PostgresPackageCacheInvalidationChannel.MaxPayloadLength + 1);

        Assert.False(PostgresPackageCacheInvalidationChannel.TryParse(
            payload,
            out _));
    }
}
