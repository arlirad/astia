namespace Pkgsrv.Application.Interfaces.Security;

public enum PackageAction
{
    Read = 0,
    Build = 1,
    Publish = 2
}

public sealed record AuthorizationResult(bool IsAllowed, string Reason)
{
    public static AuthorizationResult Allow(string reason = "allowed") => new(true, reason);

    public static AuthorizationResult Deny(string reason) => new(false, reason);
}

public sealed record AuthorizationContext
{
    private AuthorizationContext(string actorId, IReadOnlySet<string> roles, Guid? accountId)
    {
        ActorId = actorId;
        Roles = roles;
        AccountId = accountId;
    }

    public string ActorId { get; }

    public IReadOnlySet<string> Roles { get; }

    public Guid? AccountId { get; }

    public bool IsAnonymous => ActorId == Anonymous.ActorId;

    public static AuthorizationContext Anonymous { get; } = new(
        "anonymous",
        new HashSet<string>(StringComparer.OrdinalIgnoreCase),
        null);

    public static AuthorizationContext Create(
        string? actorId,
        IEnumerable<string>? roles = null,
        Guid? accountId = null)
    {
        var normalizedActorId = actorId?.Trim();

        if (string.IsNullOrWhiteSpace(normalizedActorId))
        {
            return Anonymous;
        }

        var roleSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        if (roles is not null)
        {
            foreach (var role in roles)
            {
                if (!string.IsNullOrWhiteSpace(role))
                {
                    roleSet.Add(role.Trim());
                }
            }
        }

        return new AuthorizationContext(normalizedActorId, roleSet, accountId);
    }
}
