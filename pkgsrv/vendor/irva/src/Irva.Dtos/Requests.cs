using Irva.Core.Aggregates.PermissionTrees.Entities;

namespace Irva.Dtos;

public sealed record CreateAccountRequest(string Username, string Password);

public sealed record CreateApplicationRequest(string Name);

public sealed record RenameApplicationRequest(string Name);

public sealed record CreateApplicationTokenRequest(string Name);

public sealed record AddPermissionRequest(string Name, Guid? ParentId);

public sealed record ResolvePermissionsRequest(Guid AccountId, string? ApplicationToken);

public sealed record ResolveByUsernameRequest(string Username);

public sealed record CreateRoleRequest(Guid ApplicationId, string Name);

public sealed record SetRolePermissionRequest(Guid PermissionId, ClaimStatus Status);

public sealed record ChangeUsernameRequest(string Username);

public sealed record ChangePasswordRequest(string OldPassword, string NewPassword);

public sealed record AttestRequest(string Username, string Password);

public sealed record RefreshRequest(string RefreshToken);

public sealed record RevokeRequest(string RefreshToken);

public sealed record VerifyTokenRequest(string AccessToken);