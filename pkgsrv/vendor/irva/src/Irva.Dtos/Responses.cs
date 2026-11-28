using Irva.Core.Aggregates.PermissionTrees.Entities;

namespace Irva.Dtos;

public sealed record CreateResponse(Guid Id);

public sealed record AttestResponse(string RefreshToken, string AccessToken);

public sealed record WhoAmIResponse(Guid Id, string Username);

public sealed record VerifyTokenResponse(Guid AccountId, string Username, DateTime ExpiresAt, List<AppPermissions> Applications);

public sealed record AccountSummary(Guid Id, string Username);

public sealed record ListAccountsResponse(List<AccountSummary> Accounts, int TotalCount);

public sealed record ApplicationSummary(Guid Id, string Name, DateTime? ArchivedAt);

public sealed record ListApplicationsResponse(List<ApplicationSummary> Applications, int TotalCount);

public sealed record CreateTokenResponse(Guid Id, string Token);

public sealed record ApplicationTokenSummary(Guid Id, string Name, DateTime CreatedAt, DateTime? RevokedAt);

public sealed record ListApplicationTokensResponse(List<ApplicationTokenSummary> Tokens);

public sealed record PermissionNode(Guid Id, string Name, Guid? ParentId);

public sealed record GetPermissionTreeResponse(Guid Id, string Name, List<PermissionNode> Permissions);

public sealed record RoleSummary(Guid Id, string Name);

public sealed record ListRolesResponse(List<RoleSummary> Roles);

public sealed record ResolvedPermission(Guid PermissionId, string Name, Guid? ParentId, string? FullName, ClaimStatus Status);

public sealed record ResolvePermissionsResponse(List<ResolvedPermission> Permissions)
{
    public ClaimStatus? GetStatus(string fullName)
        => Permissions.FirstOrDefault(p => p.FullName == fullName)?.Status;
}

public sealed record AppPermissions(Guid ApplicationId, string ApplicationName, List<ResolvedPermission> Permissions);

public sealed record MyPermissionsResponse(List<AppPermissions> Applications);

public sealed record RolePermissionEntry(Guid PermissionId, string FullName, ClaimStatus Status);

public sealed record GetRoleResponse(Guid Id, string Name, List<RolePermissionEntry> Permissions);

public abstract record ShootdownEvent
{
    public sealed record Targeted(Guid AccountId) : ShootdownEvent;
    public sealed record Global : ShootdownEvent;
}