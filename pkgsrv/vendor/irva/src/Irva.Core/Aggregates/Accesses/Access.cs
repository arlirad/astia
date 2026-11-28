using Irva.Core.Aggregates.Accesses.Entities;
using Irva.Core.Aggregates.Accounts;
using Irva.Core.Entities;
using Irva.Core.ValueObjects;

namespace Irva.Core.Aggregates.Accesses;

public sealed class Access : Entity
{
    public Guid AccountId
    {
        get; private set;
    }

    public string RefreshToken { get; private set; } = null!;
    public DateTime? RevokedAt
    {
        get; private set;
    }

    private readonly List<AccessToken> _accessTokens = [];
    public IReadOnlyList<AccessToken> AccessTokens => _accessTokens.AsReadOnly();

    public bool IsRevoked => RevokedAt.HasValue;

    private Access()
    {
    }

    public static Result<Access> Create(Guid accountId, string refreshToken, string accessToken, TimeSpan accessTokenLifetime)
    {
        if (accountId == Guid.Empty)
            return Result<Access>.Fail(new AccountNotFound());

        if (string.IsNullOrWhiteSpace(refreshToken) || string.IsNullOrWhiteSpace(accessToken))
            return Result<Access>.Fail(new TokenNotSpecified());

        var access = new Access {
            Id = Guid.CreateVersion7(),
            AccountId = accountId,
            RefreshToken = refreshToken,
        };

        access.IssueAccessToken(accessToken, accessTokenLifetime);
        return Result<Access>.Ok(access);
    }

    internal AccessToken IssueAccessToken(string token, TimeSpan lifetime)
    {
        var accessToken = AccessToken.Create(token, DateTime.UtcNow + lifetime);
        _accessTokens.Add(accessToken);
        return accessToken;
    }

    public Result<AccessToken> RotateRefreshToken(string newRefreshToken, string newAccessToken, TimeSpan accessTokenLifetime)
    {
        if (IsRevoked)
            return Result<AccessToken>.Fail(new TokenRevoked());

        if (string.IsNullOrWhiteSpace(newRefreshToken) || string.IsNullOrWhiteSpace(newAccessToken))
            return Result<AccessToken>.Fail(new TokenNotSpecified());

        RefreshToken = newRefreshToken;
        return Result<AccessToken>.Ok(IssueAccessToken(newAccessToken, accessTokenLifetime));
    }

    public void Revoke()
    {
        if (IsRevoked)
            return;

        RevokedAt = DateTime.UtcNow;
        foreach (var token in _accessTokens)
            token.Revoke();
    }
}
