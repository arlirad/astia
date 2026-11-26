using Pkgsrv.Application.Interfaces.Security;

namespace Pkgsrv.Presentation.Security;

public sealed class IrvaAuthenticationMiddleware
{
    private readonly RequestDelegate _next;

    public IrvaAuthenticationMiddleware(RequestDelegate next)
    {
        ArgumentNullException.ThrowIfNull(next);
        _next = next;
    }

    public async Task InvokeAsync(
        HttpContext context,
        ITransportIdentityAccessor transportIdentity,
        IIrvaIdentityVerifier identityVerifier)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(transportIdentity);
        ArgumentNullException.ThrowIfNull(identityVerifier);

        var identity = transportIdentity.GetCurrent();

        if (!string.IsNullOrWhiteSpace(identity.AccessToken))
        {
            try
            {
                var verification = await identityVerifier
                    .VerifyAsync(identity.AccessToken, context.RequestAborted)
                    .ConfigureAwait(false);

                if (verification.Identity is { } verified
                    && !verified.IsAnonymous
                    && verified.AccountId is not null)
                {
                    transportIdentity.SetVerifiedIdentity(verified);
                }
            }
            catch (OperationCanceledException) when (context.RequestAborted.IsCancellationRequested)
            {
                throw;
            }
            catch (Exception)
            {
                // Fail closed: no verified identity means downstream authorization denies.
            }
        }

        await _next(context).ConfigureAwait(false);
    }
}
