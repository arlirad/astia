using Microsoft.Extensions.Configuration;
using Pkgsrv.Presentation.Configuration;

namespace Pkgsrv.Presentation.Tests;

public sealed class PkgsrvListenOptionsTests
{
    [Fact]
    public void DefaultsToStableUnixSocketPathAndTcpDisabled()
    {
        var options = Resolve();

        Assert.Equal(PkgsrvListenOptions.DefaultUnixSocketPath, options.UnixSocketPath);
        Assert.False(options.TcpEnabled);
        Assert.Equal(PkgsrvListenOptions.DefaultTcpUrl, options.TcpUrl);
        Assert.Null(options.TcpEndpoint);
    }

    [Fact]
    public void AllowsUnixSocketPathOverride()
    {
        var options = Resolve(
            ("Pkgsrv:Listen:UnixSocketPath", "/tmp/pkgsrv/custom.sock"));

        Assert.Equal("/tmp/pkgsrv/custom.sock", options.UnixSocketPath);
        Assert.False(options.TcpEnabled);
    }

    [Fact]
    public void AllowsExplicitLoopbackTcpOverride()
    {
        var options = Resolve(
            ("Pkgsrv:Listen:Tcp:Enabled", "true"),
            ("Pkgsrv:Listen:Tcp:Url", "http://127.0.0.1:5123"));

        Assert.True(options.TcpEnabled);
        Assert.Equal("http://127.0.0.1:5123", options.TcpUrl);
        Assert.NotNull(options.TcpEndpoint);
        Assert.Equal(5123, options.TcpEndpoint.Port);
    }

    [Fact]
    public void RejectsRelativeUnixSocketOverride()
    {
        var exception = Assert.Throws<InvalidOperationException>(
            () => Resolve(("Pkgsrv:Listen:UnixSocketPath", "pkgsrv.sock")));

        Assert.Contains("absolute", exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Theory]
    [InlineData("http://0.0.0.0:5080")]
    [InlineData("https://127.0.0.1:5080")]
    [InlineData("http://127.0.0.1:5080/path")]
    public void RejectsNonLoopbackOrUnsupportedTcpUrls(string url)
    {
        Assert.Throws<InvalidOperationException>(() => Resolve(
            ("Pkgsrv:Listen:Tcp:Enabled", "true"),
            ("Pkgsrv:Listen:Tcp:Url", url)));
    }

    [Fact]
    public void RejectsInvalidTcpEnabledFlag()
    {
        Assert.Throws<InvalidOperationException>(() => Resolve(
            ("Pkgsrv:Listen:Tcp:Enabled", "sometimes")));
    }

    private static PkgsrvListenOptions Resolve(params (string Key, string? Value)[] values) =>
        PkgsrvListenOptions.FromConfiguration(
            new ConfigurationBuilder()
                .AddInMemoryCollection(values.Select(value =>
                    new KeyValuePair<string, string?>(value.Key, value.Value)))
                .Build());
}
