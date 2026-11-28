using Irva.Core.Aggregates.PermissionTrees.Entities;
using Irva.Core.Entities;
using Irva.Core.ValueObjects;

namespace Irva.Core.Aggregates.PermissionTrees;

public sealed class PermissionTree : Entity
{
    public string Name { get; private set; } = null!;

    private readonly List<Permission> _permissions = [];
    public IReadOnlyList<Permission> Permissions => _permissions.AsReadOnly();

    public const int NameMaxLength = 128;

    public static Result<PermissionTree> Create(string? name)
    {
        return string.IsNullOrWhiteSpace(name)
            ? Result<PermissionTree>.Fail(new PermissionTreeNameNotSpecified())
            : name.Length > NameMaxLength
            ? Result<PermissionTree>.Fail(new PermissionTreeNameTooLong())
            : Result<PermissionTree>.Ok(new PermissionTree {
                Id = Guid.CreateVersion7(),
                Name = name,
            });
    }

    public Result<Permission> AddPermission(string? name, Guid? parentId)
    {
        if (string.IsNullOrWhiteSpace(name))
            return Result<Permission>.Fail(new PermissionNameNotSpecified());

        if (name.Length > 128)
            return Result<Permission>.Fail(new PermissionNameTooLong());

        if (parentId.HasValue && _permissions.All(p => p.Id != parentId.Value))
            return Result<Permission>.Fail(new PermissionParentNotFound());

        if (_permissions.Any(p => p.Name == name && p.ParentId == parentId))
            return Result<Permission>.Fail(new PermissionNameDuplicate());

        var permission = new Permission(Guid.CreateVersion7(), Id, parentId, name);

        _permissions.Add(permission);
        return Result<Permission>.Ok(permission);
    }

    public void RemovePermission(Guid permissionId)
    {
        var toRemove = new HashSet<Guid> { permissionId };
        var changed = true;
        while (changed)
        {
            changed = false;
            foreach (var p in _permissions)
            {
                if (p.ParentId.HasValue && toRemove.Contains(p.ParentId.Value) && toRemove.Add(p.Id))
                    changed = true;
            }
        }

        _permissions.RemoveAll(p => toRemove.Contains(p.Id));
    }
}
