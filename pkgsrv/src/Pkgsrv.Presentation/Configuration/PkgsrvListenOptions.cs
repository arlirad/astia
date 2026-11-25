using Microsoft.Extensions.Configuration;

namespace Pkgsrv.Presentation.Configuration;

public sealed record PkgsrvListenOptions
{
    public const string DefaultUnixSocketPath = "/run/pkgsrv/pkgsrv.sock";
    public const string DefaultTcpUrl = "http://127.0.0.1:5080";

    private PkgsrvListenOptions(
        string unixSocketPath,
        bool tcpEnabled,
        string tcpUrl,
        Uri? tcpEndpoint)
    {
        UnixSocketPath = unixSocketPath;
        TcpEnabled = tcpEnabled;
        TcpUrl = tcpUrl;
        TcpEndpoint = tcpEndpoint;
    }

    public string UnixSocketPath { get; }

    public bool TcpEnabled { get; }

    public string TcpUrl { get; }

    public Uri? TcpEndpoint { get; }

    public static PkgsrvListenOptions FromConfiguration(IConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(configuration);

        var section = configuration.GetSection("Pkgsrv:Listen");
        var unixSocketPath = section["UnixSocketPath"]?.Trim();

        if (string.IsNullOrWhiteSpace(unixSocketPath))
        {
            unixSocketPath = DefaultUnixSocketPath;
        }

        if (!Path.IsPathFullyQualified(unixSocketPath))
        {
            throw new InvalidOperationException(
                "Pkgsrv:Listen:UnixSocketPath must be an absolute path.");
        }

        var tcpEnabledValue = section["Tcp:Enabled"];
        var tcpEnabled = false;

        if (!string.IsNullOrWhiteSpace(tcpEnabledValue)
            && !bool.TryParse(tcpEnabledValue, out tcpEnabled))
        {
            throw new InvalidOperationException(
                "Pkgsrv:Listen:Tcp:Enabled must be true or false.");
        }

        var tcpUrl = section["Tcp:Url"]?.Trim();

        if (string.IsNullOrWhiteSpace(tcpUrl))
        {
            tcpUrl = DefaultTcpUrl;
        }

        var tcpEndpoint = tcpEnabled ? ParseLoopbackHttpEndpoint(tcpUrl) : null;

        return new PkgsrvListenOptions(unixSocketPath, tcpEnabled, tcpUrl, tcpEndpoint);
    }

    private static Uri ParseLoopbackHttpEndpoint(string value)
    {
        if (!Uri.TryCreate(value, UriKind.Absolute, out var endpoint)
            || endpoint.Scheme != Uri.UriSchemeHttp
            || !endpoint.IsLoopback
            || endpoint.AbsolutePath != "/"
            || !string.IsNullOrEmpty(endpoint.Query)
            || !string.IsNullOrEmpty(endpoint.Fragment))
        {
            throw new InvalidOperationException(
                "Pkgsrv:Listen:Tcp:Url must be an HTTP loopback URL such as http://127.0.0.1:5080.");
        }

        return endpoint;
    }
}
