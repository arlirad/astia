namespace Pkgsrv.Application.Interfaces.Security;

public sealed class TransportIdentity
{
    private TransportIdentity(
        string? accessToken,
        AuthorizationContext actor,
        bool isVerified)
    {
        AccessToken = accessToken;
        Actor = actor;
        IsVerified = isVerified;
    }

    public string? AccessToken { get; }

    public AuthorizationContext Actor { get; }

    public bool IsVerified { get; }

    public static TransportIdentity None { get; } = new(
        null,
        AuthorizationContext.Anonymous,
        false);

    public static TransportIdentity ForRequest(string? accessToken) =>
        new(string.IsNullOrWhiteSpace(accessToken) ? null : accessToken, AuthorizationContext.Anonymous, false);

    public TransportIdentity WithVerifiedActor(AuthorizationContext actor)
    {
        ArgumentNullException.ThrowIfNull(actor);

        if (actor.IsAnonymous || actor.AccountId is null)
        {
            throw new ArgumentException(
                "A verified transport actor must carry an account id.",
                nameof(actor));
        }

        return new TransportIdentity(AccessToken, actor, true);
    }

    public override string ToString() =>
        $"TransportIdentity {{ IsVerified = {IsVerified}, ActorId = {Actor.ActorId} }}";
}

public interface ITransportIdentityAccessor
{
    TransportIdentity GetCurrent();

    void SetVerifiedIdentity(AuthorizationContext actor);
}
