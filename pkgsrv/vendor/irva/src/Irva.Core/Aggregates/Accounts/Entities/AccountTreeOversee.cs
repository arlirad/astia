namespace Irva.Core.Aggregates.Accounts.Entities;

public sealed class AccountTreeOversee
{
    public Guid AccountId
    {
        get; private set;
    }
    public Guid PermissionTreeId
    {
        get; private set;
    }

    internal AccountTreeOversee(Guid accountId, Guid permissionTreeId)
    {
        AccountId = accountId;
        PermissionTreeId = permissionTreeId;
    }
}
