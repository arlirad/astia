using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Sources;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class ConfiguredSourceAllowlistTests
{
    [Fact]
    public void ExactHost_IsAllowed()
    {
        var allowlist = new ConfiguredSourceAllowlist(["github.com"]);

        Assert.True(allowlist.IsAllowed(TestPackages.CreateManifest().Source));
    }

    [Fact]
    public void UnlistedHost_IsDenied()
    {
        var allowlist = new ConfiguredSourceAllowlist(["gitlab.com"]);

        Assert.False(allowlist.IsAllowed(TestPackages.CreateManifest().Source));
    }

    [Fact]
    public void WildcardHost_AllowsSubdomainsOnly()
    {
        var allowlist = new ConfiguredSourceAllowlist(["*.example.com"]);
        var subdomain = PackageSource.Create(
            "https://build.example.com/repo",
            TestPackages.CommitSha);
        var root = PackageSource.Create(
            "https://example.com/repo",
            TestPackages.CommitSha);

        Assert.True(allowlist.IsAllowed(subdomain));
        Assert.False(allowlist.IsAllowed(root));
    }

    [Fact]
    public void NonDefaultPort_RequiresExactPortEntry()
    {
        var source = PackageSource.Create(
            "https://github.com:8443/repo",
            TestPackages.CommitSha);

        Assert.False(new ConfiguredSourceAllowlist(["github.com"]).IsAllowed(source));
        Assert.True(new ConfiguredSourceAllowlist(["github.com:8443"]).IsAllowed(source));
        Assert.False(new ConfiguredSourceAllowlist(["github.com:443"]).IsAllowed(source));
    }

    [Fact]
    public void IpLiteral_RequiresExactEntryAndWildcardNeverMatches()
    {
        var source = PackageSource.Create(
            "https://127.0.0.1/repo",
            TestPackages.CommitSha);

        Assert.False(new ConfiguredSourceAllowlist(["*.0.0.1"]).IsAllowed(source));
        Assert.False(new ConfiguredSourceAllowlist(["github.com"]).IsAllowed(source));
        Assert.True(new ConfiguredSourceAllowlist(["127.0.0.1"]).IsAllowed(source));
    }

    [Fact]
    public void PrivateIpLiteralWithPort_RequiresExactHostAndPortEntry()
    {
        var source = PackageSource.Create(
            "https://10.0.0.7:8443/repo",
            TestPackages.CommitSha);

        Assert.False(new ConfiguredSourceAllowlist(["10.0.0.7"]).IsAllowed(source));
        Assert.False(new ConfiguredSourceAllowlist(["*.0.0.7:8443"]).IsAllowed(source));
        Assert.True(new ConfiguredSourceAllowlist(["10.0.0.7:8443"]).IsAllowed(source));
    }

    [Fact]
    public void EmptyAllowlist_DeniesEverything()
    {
        var allowlist = new ConfiguredSourceAllowlist([]);

        Assert.False(allowlist.IsAllowed(TestPackages.CreateManifest().Source));
    }

    [Fact]
    public void InvalidEntry_IsRejectedAtConfigurationTime()
    {
        Assert.Throws<ArgumentException>(() => new ConfiguredSourceAllowlist(["https://github.com"]));
    }
}
