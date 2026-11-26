using Pkgsrv.Application.Authorization;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Security;

public sealed record IrvaClientAuthorizationResponse(
    bool IsAvailable,
    bool IsAllowed,
    string Reason,
    AuthorizationContext? VerifiedContext = null,
    AuthorizationFailure Failure = AuthorizationFailure.None)
{
    public static IrvaClientAuthorizationResponse Unavailable(string reason) =>
        new(false, false, reason, null, AuthorizationFailure.Unavailable);

    public static IrvaClientAuthorizationResponse AuthenticationRequired(string reason) =>
        new(false, false, reason, null, AuthorizationFailure.AuthenticationRequired);

    public static IrvaClientAuthorizationResponse Allowed(
        AuthorizationContext verifiedContext,
        string reason = "allowed") =>
        new(true, true, reason, verifiedContext);

    public static IrvaClientAuthorizationResponse Denied(
        string reason,
        AuthorizationContext? verifiedContext = null) =>
        new(true, false, reason, verifiedContext, AuthorizationFailure.Forbidden);
}

public interface IIrvaClientAuthorizationBoundary
{
    Task<IrvaClientAuthorizationResponse> CheckAsync(
        AuthorizationContext context,
        string permission,
        PackageKey? package,
        CancellationToken cancellationToken);
}

public sealed class PendingIrvaClientAuthorizationBoundary : IIrvaClientAuthorizationBoundary
{
    private readonly ITransportIdentityAccessor _transportIdentity;

    public PendingIrvaClientAuthorizationBoundary(
        ITransportIdentityAccessor transportIdentity)
    {
        ArgumentNullException.ThrowIfNull(transportIdentity);
        _transportIdentity = transportIdentity;
    }

    public Task<IrvaClientAuthorizationResponse> CheckAsync(
        AuthorizationContext context,
        string permission,
        PackageKey? package,
        CancellationToken cancellationToken)
    {
        var transportIdentity = _transportIdentity.GetCurrent();

        return Task.FromResult(
            string.IsNullOrWhiteSpace(transportIdentity.AccessToken)
                ? IrvaClientAuthorizationResponse.AuthenticationRequired(
                    "A bearer access token is required.")
                : IrvaClientAuthorizationResponse.Unavailable(
                    "Irva authorization is disabled or not configured."));
    }
}

public sealed class IrvaClientAuthorizationBoundary : IIrvaClientAuthorizationBoundary
{
    private readonly IIrvaClientAdapter _adapter;
    private readonly ITransportIdentityAccessor _transportIdentity;

    public IrvaClientAuthorizationBoundary(
        IIrvaClientAdapter adapter,
        ITransportIdentityAccessor transportIdentity)
    {
        ArgumentNullException.ThrowIfNull(adapter);
        ArgumentNullException.ThrowIfNull(transportIdentity);

        _adapter = adapter;
        _transportIdentity = transportIdentity;
    }

    public async Task<IrvaClientAuthorizationResponse> CheckAsync(
        AuthorizationContext context,
        string permission,
        PackageKey? package,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(context);

        if (string.IsNullOrWhiteSpace(permission))
        {
            return IrvaClientAuthorizationResponse.Denied(
                "An Irva permission name is required.");
        }

        var transportIdentity = _transportIdentity.GetCurrent();
        IdentityVerificationResult verification;

        if (transportIdentity.IsVerified
            && transportIdentity.Actor.AccountId is not null)
        {
            verification = IdentityVerificationResult.Success(transportIdentity.Actor);
        }
        else if (string.IsNullOrWhiteSpace(transportIdentity.AccessToken))
        {
            return IrvaClientAuthorizationResponse.AuthenticationRequired(
                "A bearer access token is required.");
        }
        else
        {
            verification = await IrvaClientAccountVerification
                .VerifyAsync(_adapter, transportIdentity.AccessToken, cancellationToken)
                .ConfigureAwait(false);
        }

        if (!verification.IsAvailable)
        {
            return IrvaClientAuthorizationResponse.Unavailable(verification.Reason);
        }

        if (verification.Identity?.AccountId is not Guid accountId)
        {
            return IrvaClientAuthorizationResponse.AuthenticationRequired(verification.Reason);
        }

        try
        {
            var allowed = _adapter.HasPermission(accountId, permission);

            return allowed
                ? IrvaClientAuthorizationResponse.Allowed(
                    verification.Identity,
                    $"Irva verified account {accountId:D} has permission '{permission}'.")
                : IrvaClientAuthorizationResponse.Denied(
                    $"Irva verified account {accountId:D} lacks permission '{permission}'.",
                    verification.Identity);
        }
        catch (IrvaClientUnavailableException exception)
        {
            return IrvaClientAuthorizationResponse.Unavailable(exception.Message);
        }
        catch (Exception)
        {
            return IrvaClientAuthorizationResponse.Unavailable(
                "Irva authorization is unavailable.");
        }
    }
}

public sealed class IrvaClientIdentityVerifier : IIrvaIdentityVerifier
{
    private readonly IIrvaClientAdapter _adapter;

    public IrvaClientIdentityVerifier(IIrvaClientAdapter adapter)
    {
        ArgumentNullException.ThrowIfNull(adapter);
        _adapter = adapter;
    }

    public Task<IdentityVerificationResult> VerifyAsync(
        string accessToken,
        CancellationToken cancellationToken) =>
        IrvaClientAccountVerification.VerifyAsync(
            _adapter,
            accessToken,
            cancellationToken);
}

internal static class IrvaClientAccountVerification
{
    public static async Task<IdentityVerificationResult> VerifyAsync(
        IIrvaClientAdapter adapter,
        string accessToken,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(accessToken))
        {
            return IdentityVerificationResult.Invalid(
                "A bearer access token is required.");
        }

        try
        {
            var account = await adapter
                .VerifyAsync(accessToken, cancellationToken)
                .ConfigureAwait(false);
            var actorId = string.IsNullOrWhiteSpace(account.Username)
                ? account.AccountId.ToString("D")
                : account.Username;

            return IdentityVerificationResult.Success(
                AuthorizationContext.Create(actorId, accountId: account.AccountId));
        }
        catch (IrvaClientAuthenticationException exception)
        {
            return IdentityVerificationResult.Invalid(exception.Message);
        }
        catch (IrvaClientUnavailableException exception)
        {
            return IdentityVerificationResult.Unavailable(exception.Message);
        }
        catch (HttpRequestException)
        {
            return IdentityVerificationResult.Unavailable(
                "The Irva authorization endpoint is unreachable.");
        }
        catch (TaskCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            return IdentityVerificationResult.Unavailable(
                "The Irva authorization request timed out.");
        }
        catch (Exception)
        {
            return IdentityVerificationResult.Unavailable(
                "Irva authorization is unavailable.");
        }
    }
}

public sealed class PendingIrvaIdentityVerifier : IIrvaIdentityVerifier
{
    public Task<IdentityVerificationResult> VerifyAsync(
        string accessToken,
        CancellationToken cancellationToken) =>
        Task.FromResult(IdentityVerificationResult.Unavailable(
            "Irva authorization is disabled or not configured."));
}

public sealed class IrvaClientAuthorizationAdapter : IIrvaAuthorization
{
    private readonly IIrvaClientAuthorizationBoundary _boundary;

    public IrvaClientAuthorizationAdapter(IIrvaClientAuthorizationBoundary boundary)
    {
        ArgumentNullException.ThrowIfNull(boundary);
        _boundary = boundary;
    }

    public async Task<AuthorizationResult> AuthorizeAsync(
        AuthorizationContext context,
        PackageAction action,
        PackageKey? package,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(context);

        var response = await _boundary.CheckAsync(
            context,
            PackagePermissions.For(action),
            package,
            cancellationToken).ConfigureAwait(false);

        if (response.Failure == AuthorizationFailure.Unavailable)
        {
            return AuthorizationResult.Unavailable(
                $"Irva authorization is unavailable: {response.Reason}");
        }

        if (response.Failure == AuthorizationFailure.AuthenticationRequired)
        {
            return AuthorizationResult.AuthenticationRequired(response.Reason);
        }

        if (response.IsAllowed && response.VerifiedContext is not null)
        {
            return AuthorizationResult.Allow(response.VerifiedContext, response.Reason);
        }

        return AuthorizationResult.Deny(
            response.IsAllowed
                ? "Irva authorization did not return a verified account."
                : response.Reason,
            response.VerifiedContext);
    }
}
