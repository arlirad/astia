namespace Irva.Core.Aggregates.Accounts.Entities;

public sealed class AccountRoleClaim
{
    public Guid AccountId
    {
        get; private set;
    }
    public Guid RoleId
    {
        get; private set;
    }

    internal AccountRoleClaim(Guid accountId, Guid roleId)
    {
        AccountId = accountId;
        RoleId = roleId;
    }
}
