namespace Pkgsrv.Infrastructure.Security;

public static class IrvaClientAdapterFactory
{
    public static bool IsAvailable => false;

    public static IIrvaClientAdapter Create(IrvaClientConnectionSettings settings) =>
        throw new IrvaClientUnavailableException(
            "The Irva.Client project was not supplied at build time. Build with -p:IrvaClientProject=<path-to-Irva.Client.csproj>.");
}
