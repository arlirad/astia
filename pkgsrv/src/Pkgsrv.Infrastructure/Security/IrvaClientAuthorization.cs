using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Authorization;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Security;

public sealed record IrvaClientAuthorizationResponse(
    bool IsAvailable,
    bool IsAllowed,
    string Reason)
{
    public static IrvaClientAuthorizationResponse Unavailable(string reason) =>
        new(false, false, reason);

    public static IrvaClientAuthorizationResponse Allowed(string reason = "allowed") =>
        new(true, true, reason);

    public static IrvaClientAuthorizationResponse Denied(string reason) =>
        new(true, false, reason);
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
    public Task<IrvaClientAuthorizationResponse> CheckAsync(
        AuthorizationContext context,
        string permission,
        PackageKey? package,
        CancellationToken cancellationToken)
    {
        return Task.FromResult(IrvaClientAuthorizationResponse.Unavailable(
            "The Irva.Client package is not integrated in this repository."));
    }
}

public sealed class PendingIrvaClientAuthorizationAdapter : IIrvaAuthorization
{
    private readonly IIrvaClientAuthorizationBoundary _boundary;

    public PendingIrvaClientAuthorizationAdapter(IIrvaClientAuthorizationBoundary boundary)
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

        if (!response.IsAvailable)
        {
            return AuthorizationResult.Deny(
                $"Irva.Client authorization is pending: {response.Reason}");
        }

        return response.IsAllowed
            ? AuthorizationResult.Allow(response.Reason)
            : AuthorizationResult.Deny(response.Reason);
    }
}
