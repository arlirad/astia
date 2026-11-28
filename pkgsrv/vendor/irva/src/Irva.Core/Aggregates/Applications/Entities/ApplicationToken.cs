using Irva.Core.Entities;
using Irva.Core.ValueObjects;

namespace Irva.Core.Aggregates.Applications.Entities;

public class ApplicationToken : Entity
{
    public Guid ApplicationId
    {
        get; private set;
    }
    public Application Application { get; private set; } = null!;

    public string Name { get; private set; } = null!;
    public string Token { get; private set; } = null!;
    public DateTime? RevokedAt
    {
        get; private set;
    }

    public bool IsRevoked => RevokedAt.HasValue;

    internal static ApplicationToken Create(Guid applicationId, string name, string token)
    {
        return new ApplicationToken {
            Id = Guid.CreateVersion7(),
            ApplicationId = applicationId,
            Name = name,
            Token = token,
        };
    }

    internal void Revoke()
    {
        if (IsRevoked)
            return;
        RevokedAt = DateTime.UtcNow;
    }
}
