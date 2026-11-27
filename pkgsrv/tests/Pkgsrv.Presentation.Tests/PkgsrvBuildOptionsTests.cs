using Microsoft.Extensions.Configuration;
using Pkgsrv.Presentation.Configuration;

namespace Pkgsrv.Presentation.Tests;

public sealed class PkgsrvBuildOptionsTests
{
    [Fact]
    public void DefaultsToDisabledWithNoProfiles()
    {
        var options = Resolve();

        Assert.False(options.Enabled);
        Assert.Empty(options.Profiles);
        Assert.Equal("/usr/bin/bwrap", options.BubblewrapPath);
        Assert.Equal("/run/pkgsrv/builds", options.WorkspaceRoot);
        Assert.Equal(PkgsrvBuildOptions.DefaultMaxArtifactBytes, options.MaxArtifactBytes);
        Assert.Equal(0, options.WorkspaceQuotaBytes);
        Assert.Equal(0, options.WorkspaceQuotaInodes);
        Assert.False(options.AcceptNamespaceOnlyWithoutResourceLimits);
    }

    [Fact]
    public void ReadsAllowlistedProfile()
    {
        var options = Resolve(
            ("Pkgsrv:Build:Enabled", "true"),
            ("Pkgsrv:Build:AcceptNamespaceOnlyWithoutResourceLimits", "true"),
            ("Pkgsrv:Build:WorkspaceQuotaBytes", "268435456"),
            ("Pkgsrv:Build:WorkspaceQuotaInodes", "1000000"),
            ("Pkgsrv:Build:Profiles:0:ToolchainName", "gcc"),
            ("Pkgsrv:Build:Profiles:0:ToolchainVersion", "13.2.0"),
            ("Pkgsrv:Build:Profiles:0:Executable", "/usr/bin/make"),
            ("Pkgsrv:Build:Profiles:0:Arguments:0", "all"),
            ("Pkgsrv:Build:Profiles:0:OutputFile", "out/package.bin"));

        var profile = Assert.Single(options.Profiles);
        Assert.Equal("gcc", profile.ToolchainName);
        Assert.Equal("/usr/bin/make", profile.Executable);
        Assert.NotNull(profile.Arguments);
        Assert.Equal(["all"], profile.Arguments);
        Assert.Equal("out/package.bin", profile.OutputFile);
        Assert.Equal(268435456, options.WorkspaceQuotaBytes);
        Assert.Equal(1000000, options.WorkspaceQuotaInodes);
    }

    [Fact]
    public void EnabledBuildRequiresNamespaceOnlyAcknowledgement()
    {
        Assert.Throws<InvalidOperationException>(() => Resolve(
            ("Pkgsrv:Build:Enabled", "true"),
            ("Pkgsrv:Build:WorkspaceQuotaBytes", "268435456")));
    }

    [Fact]
    public void EnabledBuildRequiresWorkspaceQuota()
    {
        Assert.Throws<InvalidOperationException>(() => Resolve(
            ("Pkgsrv:Build:Enabled", "true"),
            ("Pkgsrv:Build:AcceptNamespaceOnlyWithoutResourceLimits", "true"),
            ("Pkgsrv:Build:WorkspaceQuotaInodes", "1000000")));
    }

    [Fact]
    public void EnabledBuildRequiresWorkspaceQuotaInodes()
    {
        Assert.Throws<InvalidOperationException>(() => Resolve(
            ("Pkgsrv:Build:Enabled", "true"),
            ("Pkgsrv:Build:AcceptNamespaceOnlyWithoutResourceLimits", "true"),
            ("Pkgsrv:Build:WorkspaceQuotaBytes", "268435456")));
    }

    [Theory]
    [InlineData("0")]
    [InlineData("-1")]
    [InlineData("68719476737")]
    public void RejectsInvalidWorkspaceQuota(string value)
    {
        Assert.Throws<InvalidOperationException>(() => Resolve(
            ("Pkgsrv:Build:WorkspaceQuotaBytes", value)));
    }

    [Fact]
    public void ReadsProxiedProfileNetworkMode()
    {
        var options = Resolve(
            ("Pkgsrv:Build:Enabled", "true"),
            ("Pkgsrv:Build:AcceptNamespaceOnlyWithoutResourceLimits", "true"),
            ("Pkgsrv:Build:WorkspaceQuotaBytes", "268435456"),
            ("Pkgsrv:Build:WorkspaceQuotaInodes", "1000000"),
            ("Pkgsrv:Build:Profiles:0:ToolchainName", "gcc"),
            ("Pkgsrv:Build:Profiles:0:ToolchainVersion", "13.2.0"),
            ("Pkgsrv:Build:Profiles:0:Executable", "/usr/bin/make"),
            ("Pkgsrv:Build:Profiles:0:OutputFile", "out/package.bin"),
            ("Pkgsrv:Build:Profiles:0:NetworkMode", "EgressProxy"));

        var profile = Assert.Single(options.Profiles);
        Assert.Equal("EgressProxy", profile.NetworkMode);
    }

    [Fact]
    public void RejectsUnknownProfileNetworkMode()
    {
        Assert.Throws<InvalidOperationException>(() => Resolve(
            ("Pkgsrv:Build:Enabled", "true"),
            ("Pkgsrv:Build:AcceptNamespaceOnlyWithoutResourceLimits", "true"),
            ("Pkgsrv:Build:WorkspaceQuotaBytes", "268435456"),
            ("Pkgsrv:Build:WorkspaceQuotaInodes", "1000000"),
            ("Pkgsrv:Build:Profiles:0:ToolchainName", "gcc"),
            ("Pkgsrv:Build:Profiles:0:ToolchainVersion", "13.2.0"),
            ("Pkgsrv:Build:Profiles:0:Executable", "/usr/bin/make"),
            ("Pkgsrv:Build:Profiles:0:OutputFile", "out/package.bin"),
            ("Pkgsrv:Build:Profiles:0:NetworkMode", "Direct")));
    }

    [Theory]
    [InlineData("0")]
    [InlineData("-1")]
    [InlineData("10000001")]
    public void RejectsInvalidWorkspaceQuotaInodes(string value)
    {
        Assert.Throws<InvalidOperationException>(() => Resolve(
            ("Pkgsrv:Build:WorkspaceQuotaInodes", value)));
    }

    [Fact]
    public void RejectsRelativeExecutableOrOutputTraversalWhenEnabled()
    {
        Assert.Throws<InvalidOperationException>(() => Resolve(
            ("Pkgsrv:Build:Enabled", "true"),
            ("Pkgsrv:Build:AcceptNamespaceOnlyWithoutResourceLimits", "true"),
            ("Pkgsrv:Build:WorkspaceQuotaBytes", "268435456"),
            ("Pkgsrv:Build:WorkspaceQuotaInodes", "1000000"),
            ("Pkgsrv:Build:Profiles:0:ToolchainName", "gcc"),
            ("Pkgsrv:Build:Profiles:0:ToolchainVersion", "13.2.0"),
            ("Pkgsrv:Build:Profiles:0:Executable", "make"),
            ("Pkgsrv:Build:Profiles:0:OutputFile", "out/package.bin")));

        Assert.Throws<InvalidOperationException>(() => Resolve(
            ("Pkgsrv:Build:Enabled", "true"),
            ("Pkgsrv:Build:AcceptNamespaceOnlyWithoutResourceLimits", "true"),
            ("Pkgsrv:Build:WorkspaceQuotaBytes", "268435456"),
            ("Pkgsrv:Build:WorkspaceQuotaInodes", "1000000"),
            ("Pkgsrv:Build:Profiles:0:ToolchainName", "gcc"),
            ("Pkgsrv:Build:Profiles:0:ToolchainVersion", "13.2.0"),
            ("Pkgsrv:Build:Profiles:0:Executable", "/usr/bin/make"),
            ("Pkgsrv:Build:Profiles:0:OutputFile", "../package.bin")));
    }

    [Theory]
    [InlineData("0")]
    [InlineData("-1")]
    [InlineData("1073741825")]
    public void RejectsInvalidArtifactSizeLimit(string value)
    {
        Assert.Throws<InvalidOperationException>(() => Resolve(
            ("Pkgsrv:Build:MaxArtifactBytes", value)));
    }

    private static PkgsrvBuildOptions Resolve(params (string Key, string? Value)[] values) =>
        PkgsrvBuildOptions.FromConfiguration(
            new ConfigurationBuilder()
                .AddInMemoryCollection(values.Select(value =>
                    new KeyValuePair<string, string?>(value.Key, value.Value)))
                .Build());
}
