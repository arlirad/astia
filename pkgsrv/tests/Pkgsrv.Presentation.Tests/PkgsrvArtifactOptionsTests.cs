using Microsoft.Extensions.Configuration;
using Pkgsrv.Presentation.Configuration;

namespace Pkgsrv.Presentation.Tests;

public sealed class PkgsrvArtifactOptionsTests
{
    [Fact]
    public void DefaultsToNoConfiguredArtifactRootOrSigning()
    {
        var artifacts = PkgsrvArtifactOptions.FromConfiguration(Build());
        var signing = PkgsrvSigningOptions.FromConfiguration(Build());

        Assert.Null(artifacts.Root);
        Assert.Equal(0, artifacts.QuotaBytes);
        Assert.Equal(0, artifacts.QuotaInodes);
        Assert.False(signing.CanSign);
        Assert.Null(signing.PrivateKeyPath);
    }

    [Fact]
    public void ReadsConfiguredPaths()
    {
        var configuration = Build(
            ("Pkgsrv:Artifacts:Root", "/var/lib/pkgsrv/artifacts"),
            ("Pkgsrv:Artifacts:QuotaBytes", "1073741824"),
            ("Pkgsrv:Artifacts:QuotaInodes", "1000000"),
            ("Pkgsrv:Signing:PrivateKeyPath", "/etc/pkgsrv/private.pem"),
            ("Pkgsrv:Signing:PublicKeyPath", "/etc/pkgsrv/public.pem"));

        var artifacts = PkgsrvArtifactOptions.FromConfiguration(configuration);
        var signing = PkgsrvSigningOptions.FromConfiguration(configuration);

        Assert.Equal("/var/lib/pkgsrv/artifacts", artifacts.Root);
        Assert.Equal(1073741824, artifacts.QuotaBytes);
        Assert.Equal(1000000, artifacts.QuotaInodes);
        Assert.Equal("/etc/pkgsrv/private.pem", signing.PrivateKeyPath);
        Assert.Equal("/etc/pkgsrv/public.pem", signing.PublicKeyPath);
        Assert.True(signing.CanSign);
    }

    [Fact]
    public void RejectsRelativeConfiguredPaths()
    {
        Assert.Throws<InvalidOperationException>(
            () => PkgsrvArtifactOptions.FromConfiguration(
                Build(("Pkgsrv:Artifacts:Root", "artifacts"))));

        Assert.Throws<InvalidOperationException>(
            () => PkgsrvSigningOptions.FromConfiguration(
                Build(("Pkgsrv:Signing:PrivateKeyPath", "private.pem"))));
    }

    [Theory]
    [InlineData("Pkgsrv:Artifacts:QuotaBytes", "0")]
    [InlineData("Pkgsrv:Artifacts:QuotaBytes", "-1")]
    [InlineData("Pkgsrv:Artifacts:QuotaBytes", "1099511627777")]
    [InlineData("Pkgsrv:Artifacts:QuotaInodes", "0")]
    [InlineData("Pkgsrv:Artifacts:QuotaInodes", "-1")]
    [InlineData("Pkgsrv:Artifacts:QuotaInodes", "100000001")]
    public void RejectsInvalidQuotaValues(string key, string value)
    {
        Assert.Throws<InvalidOperationException>(
            () => PkgsrvArtifactOptions.FromConfiguration(
                Build((key, value))));
    }

    private static IConfiguration Build(params (string Key, string? Value)[] values) =>
        new ConfigurationBuilder()
            .AddInMemoryCollection(values.Select(value =>
                new KeyValuePair<string, string?>(value.Key, value.Value)))
            .Build();
}
