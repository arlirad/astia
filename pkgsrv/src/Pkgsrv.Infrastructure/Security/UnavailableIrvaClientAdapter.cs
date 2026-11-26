namespace Pkgsrv.Infrastructure.Security;

public sealed class UnavailableIrvaClientAdapter : IIrvaClientAdapter
{
    private readonly string _reason;

    public UnavailableIrvaClientAdapter(string reason)
    {
        _reason = string.IsNullOrWhiteSpace(reason)
            ? "The Irva.Client integration is unavailable."
            : reason;
    }

    public Task<IrvaVerifiedAccount> VerifyAsync(
        string accessToken,
        CancellationToken cancellationToken) =>
        throw new IrvaClientUnavailableException(_reason);

    public bool HasPermission(Guid accountId, string permission) =>
        throw new IrvaClientUnavailableException(_reason);
}
