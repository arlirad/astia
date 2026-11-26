using Microsoft.Extensions.Configuration;
using Pkgsrv.Presentation.Configuration;

namespace Pkgsrv.Presentation.Tests;

public sealed class PkgsrvReadinessOptionsTests
{
    [Fact]
    public void DefaultsToBoundedTwoSecondProbe()
    {
        var options = Resolve();

        Assert.Equal(TimeSpan.FromSeconds(2), options.PostgresProbeTimeout);
    }

    [Fact]
    public void ReadsConfiguredProbeTimeout()
    {
        var options = Resolve(("Pkgsrv:Readiness:PostgresProbeTimeoutMs", "250"));

        Assert.Equal(TimeSpan.FromMilliseconds(250), options.PostgresProbeTimeout);
    }

    [Theory]
    [InlineData("")]
    [InlineData("0")]
    [InlineData("-1")]
    [InlineData("invalid")]
    [InlineData("10001")]
    public void RejectsInvalidProbeTimeout(string value)
    {
        Assert.Throws<InvalidOperationException>(
            () => Resolve(("Pkgsrv:Readiness:PostgresProbeTimeoutMs", value)));
    }

    [Fact]
    public void CreateRejectsUnboundedTimeout()
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => PkgsrvReadinessOptions.Create(TimeSpan.FromMilliseconds(
                PkgsrvReadinessOptions.MaximumPostgresProbeTimeoutMilliseconds + 1)));
    }

    private static PkgsrvReadinessOptions Resolve(params (string Key, string? Value)[] values) =>
        PkgsrvReadinessOptions.FromConfiguration(
            new ConfigurationBuilder()
                .AddInMemoryCollection(values.Select(value =>
                    new KeyValuePair<string, string?>(value.Key, value.Value)))
                .Build());
}
