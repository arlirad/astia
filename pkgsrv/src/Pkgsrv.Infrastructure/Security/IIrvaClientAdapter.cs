namespace Pkgsrv.Infrastructure.Security;

public sealed record IrvaVerifiedAccount(Guid AccountId, string Username);

public interface IIrvaClientAdapter
{
    Task<IrvaVerifiedAccount> VerifyAsync(
        string accessToken,
        CancellationToken cancellationToken);

    bool HasPermission(Guid accountId, string permission);
}

public sealed class IrvaClientAuthenticationException : Exception
{
    public IrvaClientAuthenticationException(string message)
        : base(message)
    {
    }
}

public sealed class IrvaClientUnavailableException : Exception
{
    public IrvaClientUnavailableException(string message)
        : base(message)
    {
    }
}
