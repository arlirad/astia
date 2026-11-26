namespace Pkgsrv.Application.Interfaces.Security;

public sealed record IdentityVerificationResult(
    bool IsAvailable,
    AuthorizationContext? Identity,
    string Reason)
{
    public static IdentityVerificationResult Success(AuthorizationContext identity) =>
        new(true, identity, "verified");

    public static IdentityVerificationResult Invalid(string reason) =>
        new(true, null, reason);

    public static IdentityVerificationResult Unavailable(string reason) =>
        new(false, null, reason);
}

public interface IIrvaIdentityVerifier
{
    Task<IdentityVerificationResult> VerifyAsync(
        string accessToken,
        CancellationToken cancellationToken);
}
