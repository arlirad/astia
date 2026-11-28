using Irva.Core.Entities;

namespace Irva.Core.Aggregates.PermissionTrees.Entities;

public sealed class Permission : Entity
{
    public Guid PermissionTreeId
    {
        get; init;
    }
    public Guid? ParentId
    {
        get; init;
    }
    public string Name { get; init; } = null!;

    internal Permission()
    {
    }

    internal Permission(Guid id, Guid permissionTreeId, Guid? parentId, string name)
    {
        Id = id;
        PermissionTreeId = permissionTreeId;
        ParentId = parentId;
        Name = name;
    }
}
