using Irva.Core.Aggregates.PermissionTrees.Entities;
using Irva.Dtos;

namespace Irva.Client;

public sealed partial class IrvaClient
{
    /// <summary>
    /// Creates a new application and its associated permission tree.
    /// </summary>
    /// <param name="name">The application name (max 128 characters).</param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <returns>A <see cref="CreateResponse"/> containing the new application's ID.</returns>
    /// <exception cref="IrvaApiException">Thrown if the name is duplicate or validation fails.</exception>
    public Task<CreateResponse> CreateApplicationAsync(string name, CancellationToken ct = default)
        => PostAsync<CreateResponse>("/api/applications", new {
            name,
        }, ct);

    /// <summary>
    /// Lists all (non-archived) applications.
    /// </summary>
    /// <param name="ct">Optional cancellation token.</param>
    /// <returns>All applications.</returns>
    public Task<ListApplicationsResponse> ListApplicationsAsync(CancellationToken ct = default)
        => GetAsync<ListApplicationsResponse>("/api/applications", ct);

    /// <summary>
    /// Lists applications with pagination.
    /// </summary>
    /// <param name="offset">Number of applications to skip.</param>
    /// <param name="limit">Maximum number of applications to return.</param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <returns>A paginated list of applications.</returns>
    public Task<ListApplicationsResponse> ListApplicationsAsync(int offset, int limit, CancellationToken ct = default)
        => GetAsync<ListApplicationsResponse>($"/api/applications?offset={offset}&limit={limit}", ct);

    /// <summary>
    /// Renames an application.
    /// </summary>
    /// <param name="applicationId">The application to rename.</param>
    /// <param name="name">The new name.</param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <exception cref="IrvaApiException">
    /// Thrown if the application is not found, archived, or the name is duplicate.
    /// </exception>
    public Task RenameApplicationAsync(Guid applicationId, string name, CancellationToken ct = default)
        => PutAsync($"/api/applications/{applicationId}/name", new {
            name,
        }, ct);

    /// <summary>
    /// Archives (soft-deletes) an application. Archived applications are immutable.
    /// </summary>
    /// <param name="applicationId">The application to archive.</param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <exception cref="IrvaApiException">Thrown if the application is not found.</exception>
    public Task ArchiveApplicationAsync(Guid applicationId, CancellationToken ct = default)
        => PutAsync($"/api/applications/{applicationId}/archive", null, ct);

    /// <summary>
    /// Permanently deletes an archived application.
    /// </summary>
    /// <param name="applicationId">The archived application to delete.</param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <exception cref="IrvaApiException">
    /// Thrown if the application is not found or not yet archived.
    /// </exception>
    public Task DeleteApplicationAsync(Guid applicationId, CancellationToken ct = default)
        => DeleteAsync($"/api/applications/{applicationId}", ct);

    /// <summary>
    /// Issues a named API token for an application.
    /// </summary>
    /// <param name="applicationId">The application to issue a token for.</param>
    /// <param name="name">A descriptive name for the token.</param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <returns>A <see cref="CreateTokenResponse"/> with the token ID and raw secret.</returns>
    /// <exception cref="IrvaApiException">
    /// Thrown if the application is not found or archived.
    /// </exception>
    public Task<CreateTokenResponse> CreateApplicationTokenAsync(Guid applicationId, string name, CancellationToken ct = default)
        => PostAsync<CreateTokenResponse>($"/api/applications/{applicationId}/tokens", new {
            name,
        }, ct);

    /// <summary>
    /// Revokes an application token, preventing further use.
    /// </summary>
    /// <param name="applicationId">The application the token belongs to.</param>
    /// <param name="tokenId">The token to revoke.</param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <exception cref="IrvaApiException">Thrown if the application or token is not found.</exception>
    public Task RevokeApplicationTokenAsync(Guid applicationId, Guid tokenId, CancellationToken ct = default)
        => PostAsync($"/api/applications/{applicationId}/tokens/{tokenId}/revoke", ct);

    /// <summary>
    /// Lists all tokens for an application.
    /// </summary>
    /// <param name="applicationId">The application whose tokens to list.</param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <returns>All tokens for the application, including revoked ones.</returns>
    public Task<ListApplicationTokensResponse> ListApplicationTokensAsync(Guid applicationId, CancellationToken ct = default)
        => GetAsync<ListApplicationTokensResponse>($"/api/applications/{applicationId}/tokens", ct);

    /// <summary>
    /// Adds a permission node to an application's permission tree.
    /// </summary>
    /// <param name="applicationId">The application to add the permission to.</param>
    /// <param name="name">The permission node name (unique within its parent).</param>
    /// <param name="parentId">
    /// The parent permission ID, or <see langword="null"/> to create a root node.
    /// </param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <returns>A <see cref="CreateResponse"/> containing the new permission's ID.</returns>
    /// <exception cref="IrvaApiException">
    /// Thrown if the application, parent permission is not found, or the name is duplicate.
    /// </exception>
    public Task<CreateResponse> AddPermissionAsync(Guid applicationId, string name, Guid? parentId = null, CancellationToken ct = default)
        => PostAsync<CreateResponse>($"/api/applications/{applicationId}/permissions", new {
            name,
            parentId,
        }, ct);

    /// <summary>
    /// Gets the full permission tree for an application.
    /// </summary>
    /// <param name="applicationId">The application whose tree to retrieve.</param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <returns>The permission tree with all nodes.</returns>
    /// <exception cref="IrvaApiException">Thrown if the application is not found.</exception>
    public Task<GetPermissionTreeResponse> GetPermissionTreeAsync(Guid applicationId, CancellationToken ct = default)
        => GetAsync<GetPermissionTreeResponse>($"/api/applications/{applicationId}/tree", ct);

    /// <summary>
    /// Lists all roles for an application.
    /// </summary>
    /// <param name="applicationId">The application whose roles to list.</param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <returns>All roles belonging to the application's permission tree.</returns>
    public Task<ListRolesResponse> ListRolesAsync(Guid applicationId, CancellationToken ct = default)
        => GetAsync<ListRolesResponse>($"/api/applications/{applicationId}/roles", ct);

    /// <summary>
    /// Resolves all permissions for an account within the application identified by the
    /// configured app token.
    /// </summary>
    /// <param name="accountId">The account whose permissions to resolve.</param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <returns>The effective permission set for the account.</returns>
    /// <exception cref="IrvaApiException">
    /// Thrown if the app token is invalid or the account is not found.
    /// </exception>
    /// <remarks>
    /// Requires the client to be configured with an <see cref="IrvaClientOptions.AppToken"/>.
    /// </remarks>
    public Task<ResolvePermissionsResponse> ResolvePermissionsAsync(Guid accountId, CancellationToken ct = default)
        => PostAsync<ResolvePermissionsResponse>("/api/applications/resolve", new {
            accountId,
        }, ct);

    public Task<ResolvePermissionsResponse> ResolveByUsernameAsync(string username, CancellationToken ct = default)
        => PostAsync<ResolvePermissionsResponse>("/api/applications/resolve-by-username", new {
            username,
        }, ct);

    public bool HasPermission(Guid accountId, string fullName)
        => _permissionCache.TryGetValue(accountId, out var permissions) && permissions.GetValueOrDefault(fullName) == ClaimStatus.Permit;

    public async Task<bool> HasPermissionAsync(Guid accountId, string fullName, CancellationToken ct = default)
    {
        var result = await ResolvePermissionsAsync(accountId, ct);
        return result.GetStatus(fullName) == ClaimStatus.Permit;
    }

    public async Task<bool> HasPermissionByUsernameAsync(string username, string fullName, CancellationToken ct = default)
    {
        var result = await ResolveByUsernameAsync(username, ct);
        return result.GetStatus(fullName) == ClaimStatus.Permit;
    }

    /// <summary>
    /// Removes a permission node (and all its descendants) from an application's tree.
    /// </summary>
    /// <param name="applicationId">The application the permission belongs to.</param>
    /// <param name="permissionId">The permission node to remove.</param>
    /// <param name="ct">Optional cancellation token.</param>
    /// <exception cref="IrvaApiException">
    /// Thrown if the application or permission is not found.
    /// </exception>
    public Task RemovePermissionAsync(Guid applicationId, Guid permissionId, CancellationToken ct = default)
        => DeleteAsync($"/api/applications/{applicationId}/permissions/{permissionId}", ct);
}
