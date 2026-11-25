using Microsoft.Extensions.Configuration;
using Pkgsrv.Presentation.Configuration;

namespace Pkgsrv.Presentation.Tests;

public sealed class PkgsrvListenOptionsTests
{
    private const string EnvironmentPrefix = "PKGSRV_TEST_LISTEN_";
    private const string EnvironmentVariable =
        EnvironmentPrefix + "PKGSRV__LISTEN__UNIXSOCKETPATH";

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
    public void MissingUnixSocketKeyUsesDefaultWhenListenSectionExists()
    {
        var options = Resolve(
            ("Pkgsrv:Listen:Tcp:Url", "http://127.0.0.1:5080"));

        Assert.Equal(PkgsrvListenOptions.DefaultUnixSocketPath, options.UnixSocketPath);
    }

    [Theory]
    [InlineData("")]
    [InlineData("   ")]
    public void RejectsExplicitEmptyUnixSocketPath(string value)
    {
        var exception = Assert.Throws<InvalidOperationException>(
            () => Resolve(("Pkgsrv:Listen:UnixSocketPath", value)));

        Assert.Contains("supplied but is empty", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void ExplicitEmptyCommandLineValueIsRejectedInsteadOfFallingBackToAppConfig()
    {
        var configuration = new ConfigurationBuilder()
            .AddInMemoryCollection(new Dictionary<string, string?>
            {
                ["Pkgsrv:Listen:UnixSocketPath"] = "/app/pkgsrv.sock"
            })
            .AddCommandLine(["--Pkgsrv:Listen:UnixSocketPath="])
            .Build();

        Assert.Throws<InvalidOperationException>(
            () => PkgsrvListenOptions.FromConfiguration(configuration));
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
    public void EnvironmentOverridesAppConfig()
    {
        WithEnvironmentVariable(
            EnvironmentVariable,
            "/env/pkgsrv.sock",
            () =>
            {
                var configuration = new ConfigurationBuilder()
                    .AddInMemoryCollection(new Dictionary<string, string?>
                    {
                        ["Pkgsrv:Listen:UnixSocketPath"] = "/app/pkgsrv.sock"
                    })
                    .AddEnvironmentVariables(EnvironmentPrefix)
                    .Build();

                var options = PkgsrvListenOptions.FromConfiguration(configuration);

                Assert.Equal("/env/pkgsrv.sock", options.UnixSocketPath);
            });
    }

    [Fact]
    public void CommandLineOverridesEnvironmentAndAppConfig()
    {
        WithEnvironmentVariable(
            EnvironmentVariable,
            "/env/pkgsrv.sock",
            () =>
            {
                var configuration = new ConfigurationBuilder()
                    .AddInMemoryCollection(new Dictionary<string, string?>
                    {
                        ["Pkgsrv:Listen:UnixSocketPath"] = "/app/pkgsrv.sock"
                    })
                    .AddEnvironmentVariables(EnvironmentPrefix)
                    .AddCommandLine(["--Pkgsrv:Listen:UnixSocketPath=/cli/pkgsrv.sock"])
                    .Build();

                var options = PkgsrvListenOptions.FromConfiguration(configuration);

                Assert.Equal("/cli/pkgsrv.sock", options.UnixSocketPath);
            });
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

    [Theory]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData("sometimes")]
    public void RejectsInvalidTcpEnabledFlag(string value)
    {
        Assert.Throws<InvalidOperationException>(() => Resolve(
            ("Pkgsrv:Listen:Tcp:Enabled", value)));
    }

    [Theory]
    [InlineData("")]
    [InlineData("   ")]
    public void RejectsExplicitEmptyTcpUrl(string value)
    {
        Assert.Throws<InvalidOperationException>(() => Resolve(
            ("Pkgsrv:Listen:Tcp:Enabled", "true"),
            ("Pkgsrv:Listen:Tcp:Url", value)));
    }

    private static PkgsrvListenOptions Resolve(params (string Key, string? Value)[] values) =>
        PkgsrvListenOptions.FromConfiguration(
            new ConfigurationBuilder()
                .AddInMemoryCollection(values.Select(value =>
                    new KeyValuePair<string, string?>(value.Key, value.Value)))
                .Build());

    private static void WithEnvironmentVariable(string name, string value, Action action)
    {
        var previous = Environment.GetEnvironmentVariable(name);

        try
        {
            Environment.SetEnvironmentVariable(name, value);
            action();
        }
        finally
        {
            Environment.SetEnvironmentVariable(name, previous);
        }
    }
}
