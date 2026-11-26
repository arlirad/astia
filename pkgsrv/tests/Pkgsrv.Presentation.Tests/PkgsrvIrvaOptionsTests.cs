using Microsoft.Extensions.Configuration;
using Pkgsrv.Presentation.Configuration;

namespace Pkgsrv.Presentation.Tests;

public sealed class PkgsrvIrvaOptionsTests
{
    [Fact]
    public void DisabledByDefault()
    {
        var options = Resolve();

        Assert.False(options.Enabled);
        Assert.False(options.Required);
        Assert.False(options.IsComplete);
    }

    [Fact]
    public void EnabledWithoutConnectionSettingsIsIncompleteButConfigurable()
    {
        var options = Resolve(("Pkgsrv:Irva:Enabled", "true"));

        Assert.True(options.Enabled);
        Assert.False(options.Required);
        Assert.False(options.IsComplete);
    }

    [Fact]
    public void EnabledWithHttpsSettingsIsComplete()
    {
        var options = Resolve(
            ("Pkgsrv:Irva:Enabled", "true"),
            ("Pkgsrv:Irva:Required", "true"),
            ("Pkgsrv:Irva:BaseUrl", "https://irva.example"),
            ("Pkgsrv:Irva:AppToken", "configured-secret"),
            ("Pkgsrv:Irva:SocketAddress", "/run/irva/irva.sock"));

        Assert.True(options.Enabled);
        Assert.True(options.Required);
        Assert.True(options.IsComplete);
        Assert.Equal("https://irva.example", options.BaseUrl);
        Assert.Equal("configured-secret", options.AppToken);
        Assert.Equal("/run/irva/irva.sock", options.SocketAddress);
    }

    [Fact]
    public void RequiredWithoutEnabledIsRejected()
    {
        var exception = Assert.Throws<InvalidOperationException>(
            () => Resolve(("Pkgsrv:Irva:Required", "true")));

        Assert.Contains("Enabled=true", exception.Message, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData("not-a-url")]
    [InlineData("ftp://irva.example")]
    [InlineData("https://user:password@irva.example")]
    [InlineData("https://irva.example?token=secret")]
    public void InvalidBaseUrlIsRejected(string baseUrl)
    {
        Assert.Throws<InvalidOperationException>(
            () => Resolve(
                ("Pkgsrv:Irva:Enabled", "true"),
                ("Pkgsrv:Irva:BaseUrl", baseUrl),
                ("Pkgsrv:Irva:AppToken", "configured-secret")));
    }

    [Fact]
    public void HttpNonLocalRequiresSocketAddress()
    {
        Assert.Throws<InvalidOperationException>(
            () => Resolve(
                ("Pkgsrv:Irva:Enabled", "true"),
                ("Pkgsrv:Irva:BaseUrl", "http://irva.example"),
                ("Pkgsrv:Irva:AppToken", "configured-secret")));

        var options = Resolve(
            ("Pkgsrv:Irva:Enabled", "true"),
            ("Pkgsrv:Irva:BaseUrl", "http://irva.example"),
            ("Pkgsrv:Irva:AppToken", "configured-secret"),
            ("Pkgsrv:Irva:SocketAddress", "/run/irva/irva.sock"));

        Assert.True(options.IsComplete);
    }

    [Fact]
    public void LocalhostHttpIsAllowedWithoutSocketAddress()
    {
        var options = Resolve(
            ("Pkgsrv:Irva:Enabled", "true"),
            ("Pkgsrv:Irva:BaseUrl", "http://127.0.0.1:8080"),
            ("Pkgsrv:Irva:AppToken", "configured-secret"));

        Assert.True(options.IsComplete);
    }

    [Theory]
    [InlineData("irva.sock")]
    [InlineData("./irva.sock")]
    public void RelativeSocketAddressIsRejected(string socketAddress)
    {
        Assert.Throws<InvalidOperationException>(
            () => Resolve(
                ("Pkgsrv:Irva:Enabled", "true"),
                ("Pkgsrv:Irva:BaseUrl", "https://irva.example"),
                ("Pkgsrv:Irva:AppToken", "configured-secret"),
                ("Pkgsrv:Irva:SocketAddress", socketAddress)));
    }

    [Fact]
    public void ToStringDoesNotExposeAppToken()
    {
        const string token = "super-secret-app-token";
        var options = Resolve(
            ("Pkgsrv:Irva:Enabled", "true"),
            ("Pkgsrv:Irva:BaseUrl", "https://irva.example"),
            ("Pkgsrv:Irva:AppToken", token));

        Assert.DoesNotContain(token, options.ToString(), StringComparison.Ordinal);
    }

    private static PkgsrvIrvaOptions Resolve(params (string Key, string Value)[] values)
    {
        var configuration = new ConfigurationBuilder()
            .AddInMemoryCollection(values.ToDictionary(
                value => value.Key,
                value => (string?)value.Value))
            .Build();

        return PkgsrvIrvaOptions.FromConfiguration(configuration);
    }
}
