namespace Irva.Client;

public sealed class IrvaClientOptions
{
    public required string BaseUrl
    {
        get; init;
    }

    public string? SocketAddress
    {
        get; init;
    }

    public string? MasterKey
    {
        get; init;
    }

    public string? AppToken
    {
        get; init;
    }
}
