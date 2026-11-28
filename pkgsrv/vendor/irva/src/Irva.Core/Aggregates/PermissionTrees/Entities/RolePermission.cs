namespace Irva.Core.Aggregates.PermissionTrees.Entities;

public sealed class RolePermission
{
    public Guid RoleId
    {
        get; init;
    }
    public Guid PermissionId
    {
        get; init;
    }
    public ClaimStatus Status
    {
        get; private set;
    }

    internal RolePermission()
    {
    }

    internal RolePermission(Guid roleId, Guid permissionId, ClaimStatus status)
    {
        RoleId = roleId;
        PermissionId = permissionId;
        Status = status;
    }

    internal void UpdateStatus(ClaimStatus status) => Status = status;
}
