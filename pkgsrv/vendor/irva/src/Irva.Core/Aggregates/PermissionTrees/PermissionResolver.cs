using Irva.Core.Aggregates.PermissionTrees.Entities;

namespace Irva.Core.Aggregates.PermissionTrees;

public static class PermissionResolver
{
    public static Dictionary<Guid, ClaimStatus> Resolve(
        IReadOnlyList<Permission> treePermissions,
        IReadOnlyList<Role> roles)
    {
        var lookup = treePermissions.ToDictionary(p => p.Id);
        var result = new Dictionary<Guid, ClaimStatus>();

        if (roles.Count == 0)
            return result;

        foreach (var permission in treePermissions)
        {
            var mergedStatus = (ClaimStatus?)null;

            foreach (var role in roles)
            {
                var status = ResolveInTree(permission.Id, role, lookup);
                if (status is null)
                    continue;

                if (status == ClaimStatus.Permit)
                    mergedStatus = ClaimStatus.Permit;
                else
                    mergedStatus ??= status;
            }

            result[permission.Id] = mergedStatus ?? ClaimStatus.Disallow;
        }

        return result;
    }

    private static ClaimStatus? ResolveInTree(
        Guid permissionId,
        Role role,
        Dictionary<Guid, Permission> lookup)
    {
        var currentId = (Guid?)permissionId;

        while (currentId.HasValue)
        {
            if (!lookup.TryGetValue(currentId.Value, out var node))
                return null;

            var entry = role.Permissions.FirstOrDefault(p => p.PermissionId == node.Id);
            if (entry is not null && entry.Status != ClaimStatus.Inherit)
                return entry.Status;

            currentId = node.ParentId;
        }

        return null;
    }
}
