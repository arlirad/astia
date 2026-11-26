namespace Pkgsrv.Infrastructure.Security;

public sealed record IrvaClientConnectionSettings(
    string BaseUrl,
    string AppToken,
    string? SocketAddress)
{
    public override string ToString() =>
        $"IrvaClientConnectionSettings {{ BaseUrl = {BaseUrl}, HasAppToken = {!string.IsNullOrWhiteSpace(AppToken)}, HasSocketAddress = {SocketAddress is not null} }}";
}
