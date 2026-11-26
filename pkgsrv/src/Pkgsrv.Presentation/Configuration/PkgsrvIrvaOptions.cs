using Microsoft.Extensions.Configuration;
using Pkgsrv.Infrastructure.Security;

namespace Pkgsrv.Presentation.Configuration;

public sealed class PkgsrvIrvaOptions
{
    private PkgsrvIrvaOptions(
        bool enabled,
        bool required,
        string? baseUrl,
        string? appToken,
        string? socketAddress)
    {
        Enabled = enabled;
        Required = required;
        BaseUrl = baseUrl;
        AppToken = appToken;
        SocketAddress = socketAddress;
    }

    public bool Enabled { get; }

    public bool Required { get; }

    public string? BaseUrl { get; }

    public string? AppToken { get; }

    public string? SocketAddress { get; }

    public bool IsComplete =>
        Enabled
        && !string.IsNullOrWhiteSpace(BaseUrl)
        && !string.IsNullOrWhiteSpace(AppToken);

    public static PkgsrvIrvaOptions Disabled { get; } = new(
        false,
        false,
        null,
        null,
        null);

    public static PkgsrvIrvaOptions FromConfiguration(IConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(configuration);

        var section = configuration.GetSection("Pkgsrv:Irva");
        var enabled = ReadBoolean(section, "Enabled", "Pkgsrv:Irva:Enabled");
        var required = ReadBoolean(section, "Required", "Pkgsrv:Irva:Required");

        if (required && !enabled)
        {
            throw new InvalidOperationException(
                "Pkgsrv:Irva:Required requires Pkgsrv:Irva:Enabled=true.");
        }

        if (!enabled)
        {
            return Disabled;
        }

        var baseUrl = ReadOptional(section, "BaseUrl", "Pkgsrv:Irva:BaseUrl");
        var appToken = ReadOptional(section, "AppToken", "Pkgsrv:Irva:AppToken");
        var socketAddress = ReadOptional(section, "SocketAddress", "Pkgsrv:Irva:SocketAddress");

        ValidateBaseUrl(baseUrl, socketAddress);
        ValidateSocketAddress(socketAddress);

        return new PkgsrvIrvaOptions(enabled, required, baseUrl, appToken, socketAddress);
    }

    public IrvaClientConnectionSettings ToConnectionSettings()
    {
        if (!IsComplete)
        {
            throw new InvalidOperationException(
                "Pkgsrv:Irva:BaseUrl and Pkgsrv:Irva:AppToken are required for the Irva.Client integration.");
        }

        return new IrvaClientConnectionSettings(BaseUrl!, AppToken!, SocketAddress);
    }

    public override string ToString() =>
        $"PkgsrvIrvaOptions {{ Enabled = {Enabled}, Required = {Required}, HasBaseUrl = {BaseUrl is not null}, HasAppToken = {AppToken is not null}, HasSocketAddress = {SocketAddress is not null} }}";

    private static bool ReadBoolean(
        IConfigurationSection section,
        string relativeKey,
        string fullKey)
    {
        var value = section[relativeKey];

        if (value is null)
        {
            return false;
        }

        if (string.IsNullOrWhiteSpace(value) || !bool.TryParse(value, out var parsed))
        {
            throw new InvalidOperationException(
                $"{fullKey} must be true or false when supplied.");
        }

        return parsed;
    }

    private static string? ReadOptional(
        IConfigurationSection section,
        string relativeKey,
        string fullKey)
    {
        var value = section[relativeKey];

        if (value is null)
        {
            return null;
        }

        if (string.IsNullOrWhiteSpace(value))
        {
            throw new InvalidOperationException(
                $"{fullKey} was supplied but is empty. Omit the key to leave it unset.");
        }

        return value.Trim();
    }

    private static void ValidateBaseUrl(string? baseUrl, string? socketAddress)
    {
        if (baseUrl is null)
        {
            return;
        }

        if (!Uri.TryCreate(baseUrl, UriKind.Absolute, out var endpoint)
            || (endpoint.Scheme != Uri.UriSchemeHttps && endpoint.Scheme != Uri.UriSchemeHttp)
            || string.IsNullOrWhiteSpace(endpoint.Host)
            || !string.IsNullOrEmpty(endpoint.UserInfo)
            || !string.IsNullOrEmpty(endpoint.Query)
            || !string.IsNullOrEmpty(endpoint.Fragment))
        {
            throw new InvalidOperationException(
                "Pkgsrv:Irva:BaseUrl must be an absolute HTTP(S) URL without credentials, query, or fragment.");
        }

        if (endpoint.Scheme == Uri.UriSchemeHttps || socketAddress is not null)
        {
            return;
        }

        var isLocalHost = endpoint.Host.Equals("localhost", StringComparison.OrdinalIgnoreCase)
            || endpoint.Host == "127.0.0.1"
            || endpoint.Host == "::1";

        if (!isLocalHost)
        {
            throw new InvalidOperationException(
                "Pkgsrv:Irva:BaseUrl must use HTTPS unless it targets localhost or an explicit Unix socket address is configured.");
        }
    }

    private static void ValidateSocketAddress(string? socketAddress)
    {
        if (socketAddress is null)
        {
            return;
        }

        if (!Path.IsPathFullyQualified(socketAddress)
            || socketAddress.Contains('\0', StringComparison.Ordinal))
        {
            throw new InvalidOperationException(
                "Pkgsrv:Irva:SocketAddress must be an absolute Unix domain socket path.");
        }
    }
}
