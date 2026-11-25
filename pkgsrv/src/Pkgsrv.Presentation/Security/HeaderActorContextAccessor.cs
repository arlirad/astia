using Pkgsrv.Application.Abstractions.Security;

namespace Pkgsrv.Presentation.Security;

public interface IActorContextAccessor
{
    AuthorizationContext GetCurrent();
}

public sealed class HeaderActorContextAccessor : IActorContextAccessor
{
    public const string ActorHeader = "X-Pkgsrv-Actor";
    public const string RolesHeader = "X-Pkgsrv-Roles";
    public const string AccountHeader = "X-Pkgsrv-Account";

    private readonly IHttpContextAccessor _httpContextAccessor;

    public HeaderActorContextAccessor(IHttpContextAccessor httpContextAccessor)
    {
        ArgumentNullException.ThrowIfNull(httpContextAccessor);
        _httpContextAccessor = httpContextAccessor;
    }

    public AuthorizationContext GetCurrent()
    {
        var headers = _httpContextAccessor.HttpContext?.Request.Headers;
        var actorId = headers?[ActorHeader].ToString();
        var rolesHeader = headers?[RolesHeader].ToString();
        var accountHeader = headers?[AccountHeader].ToString();
        var roles = string.IsNullOrWhiteSpace(rolesHeader)
            ? Array.Empty<string>()
            : rolesHeader.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        var accountId = Guid.TryParse(accountHeader, out var parsedAccountId)
            ? parsedAccountId
            : (Guid?)null;

        return AuthorizationContext.Create(actorId, roles, accountId);
    }
}
