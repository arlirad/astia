using Irva.Core.Entities;

namespace Irva.Core.Aggregates.Accesses.Entities;

public class AccessToken : Entity
{
    public Guid AccessId
    {
        get; private set;
    }
    public Access Access { get; private set; } = null!;

    public string Token { get; private set; } = null!;
    public DateTime ExpiresAt
    {
        get; private set;
    }
    public DateTime? RevokedAt
    {
        get; private set;
    }

    public bool IsRevoked => RevokedAt.HasValue;
    public bool IsExpired => ExpiresAt <= DateTime.UtcNow;
    public bool IsValid => !IsRevoked && !IsExpired;

    internal static AccessToken Create(string token, DateTime expiresAt)
    {
        return new AccessToken {
            Id = Guid.CreateVersion7(),
            Token = token,
            ExpiresAt = expiresAt,
        };
    }

    internal void Revoke()
    {
        if (IsRevoked)
            return;
        RevokedAt = DateTime.UtcNow;
    }
}
