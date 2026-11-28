using Irva.Core.Entities;
using Irva.Core.ValueObjects;

namespace Irva.Core.Aggregates.PermissionTrees.Entities;

public sealed class Role : Entity
{
    public Guid PermissionTreeId
    {
        get; private set;
    }
    public string Name { get; private set; } = null!;

    private readonly List<RolePermission> _permissions = [];
    public IReadOnlyList<RolePermission> Permissions => _permissions.AsReadOnly();

    public const int NameMaxLength = 128;

    public static Result<Role> Create(Guid permissionTreeId, string? name)
    {
        return string.IsNullOrWhiteSpace(name)
            ? Result<Role>.Fail(new RoleNameNotSpecified())
            : name.Length > NameMaxLength
            ? Result<Role>.Fail(new RoleNameTooLong())
            : Result<Role>.Ok(new Role {
                Id = Guid.CreateVersion7(),
                PermissionTreeId = permissionTreeId,
                Name = name,
            });
    }

    public Result<RolePermission> SetPermission(Guid permissionId, ClaimStatus status, Func<Guid, bool> permissionExists)
    {
        if (!Enum.IsDefined(status))
            return Result<RolePermission>.Fail(new InvalidClaimStatus());

        if (!permissionExists(permissionId))
            return Result<RolePermission>.Fail(new PermissionNotFound());

        var existing = _permissions.FirstOrDefault(p => p.PermissionId == permissionId);
        if (existing is not null)
        {
            existing.UpdateStatus(status);
            return Result<RolePermission>.Ok(existing);
        }

        var rp = new RolePermission(Id, permissionId, status);
        _permissions.Add(rp);
        return Result<RolePermission>.Ok(rp);
    }

    public void RemovePermission(Guid permissionId) => _permissions.RemoveAll(p => p.PermissionId == permissionId);
}
