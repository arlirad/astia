using Irva.Dtos;

namespace Irva.Client;

public sealed partial class IrvaClient
{
    /// <summary>
    /// Creates a new role within a permission tree.
    /// </summary>
    /// <param name="permissionTreeId">
    /// The permission tree the role belongs to (each application has one).
    /// </param>
    /// <param name="name">The role name (max 128 characters, unique within the tree).</param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <returns>A <see cref="CreateResponse"/> containing the new role's ID.</returns>
    /// <exception cref="IrvaApiException">
    /// Thrown if the permission tree is not found or the name is duplicate.
    /// </exception>
    public Task<CreateResponse> CreateRoleAsync(Guid permissionTreeId, string name, CancellationToken ct = default)
        => PostAsync<CreateResponse>("/api/roles", new {
            permissionTreeId,
            name,
        }, ct);

    /// <summary>
    /// Sets a permission claim on a role.
    /// </summary>
    /// <param name="roleId">The role to update.</param>
    /// <param name="permissionId">The permission node to set the claim on.</param>
    /// <param name="status">
    /// The claim status: <c>0</c> = Inherit, <c>1</c> = Permit, <c>2</c> = Disallow.
    /// </param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <exception cref="IrvaApiException">
    /// Thrown if the role or permission is not found, or the status is invalid.
    /// </exception>
    public Task SetRolePermissionAsync(Guid roleId, Guid permissionId, int status, CancellationToken ct = default)
        => PostAsync($"/api/roles/{roleId}/permissions", new {
            permissionId,
            status,
        }, ct);

    /// <summary>
    /// Deletes a role.
    /// </summary>
    /// <param name="roleId">The role to delete.</param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <exception cref="IrvaApiException">Thrown if the role is not found.</exception>
    public Task DeleteRoleAsync(Guid roleId, CancellationToken ct = default)
        => DeleteAsync($"/api/roles/{roleId}", ct);

    /// <summary>
    /// Removes a permission override from a role, reverting it to inherited behavior.
    /// </summary>
    /// <param name="roleId">The role to update.</param>
    /// <param name="permissionId">The permission claim to remove.</param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <exception cref="IrvaApiException">
    /// Thrown if the role or permission claim is not found.
    /// </exception>
    public Task RemoveRolePermissionAsync(Guid roleId, Guid permissionId, CancellationToken ct = default)
        => DeleteAsync($"/api/roles/{roleId}/permissions/{permissionId}", ct);
}
