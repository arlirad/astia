using Microsoft.Extensions.Primitives;
using Pkgsrv.Application.Interfaces.Security;

namespace Pkgsrv.Presentation.Security;

public interface IActorContextAccessor
{
    AuthorizationContext GetCurrent();
}

public sealed class HttpTransportIdentityAccessor : ITransportIdentityAccessor, IActorContextAccessor
{
    private const string VerifiedIdentityItemKey = "Pkgsrv.VerifiedIdentity";

    private readonly IHttpContextAccessor _httpContextAccessor;

    public HttpTransportIdentityAccessor(IHttpContextAccessor httpContextAccessor)
    {
        ArgumentNullException.ThrowIfNull(httpContextAccessor);
        _httpContextAccessor = httpContextAccessor;
    }

    public TransportIdentity GetCurrent()
    {
        var httpContext = _httpContextAccessor.HttpContext;

        if (httpContext is null)
        {
            return TransportIdentity.None;
        }

        var accessToken = ReadBearerToken(httpContext.Request.Headers.Authorization);
        var identity = TransportIdentity.ForRequest(accessToken);

        if (httpContext.Items[VerifiedIdentityItemKey] is AuthorizationContext verified
            && !verified.IsAnonymous
            && verified.AccountId is not null)
        {
            return identity.WithVerifiedActor(verified);
        }

        return identity;
    }

    public void SetVerifiedIdentity(AuthorizationContext actor)
    {
        ArgumentNullException.ThrowIfNull(actor);

        if (actor.IsAnonymous || actor.AccountId is null)
        {
            throw new ArgumentException(
                "A verified identity must carry an account id.",
                nameof(actor));
        }

        var httpContext = _httpContextAccessor.HttpContext
            ?? throw new InvalidOperationException(
                "No active HTTP request is available for verified identity propagation.");
        httpContext.Items[VerifiedIdentityItemKey] = actor;
    }

    AuthorizationContext IActorContextAccessor.GetCurrent() => GetCurrent().Actor;

    private static string? ReadBearerToken(StringValues authorizationValues)
    {
        if (authorizationValues.Count != 1)
        {
            return null;
        }

        var value = authorizationValues[0];

        if (string.IsNullOrWhiteSpace(value))
        {
            return null;
        }

        var parts = value.Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);

        if (parts.Length != 2
            || !parts[0].Equals("Bearer", StringComparison.OrdinalIgnoreCase)
            || string.IsNullOrWhiteSpace(parts[1]))
        {
            return null;
        }

        return parts[1].Trim();
    }
}
