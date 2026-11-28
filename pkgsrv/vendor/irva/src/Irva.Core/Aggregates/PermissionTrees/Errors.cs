using Irva.Core.ValueObjects;

namespace Irva.Core.Aggregates.PermissionTrees;

public class PermissionTreeNameNotSpecified() : Error("permission_tree.name.not_specified");
public class PermissionTreeNameTooLong() : Error("permission_tree.name.too_long");
public class PermissionTreeNotFound() : Error("permission_tree.not_found");
public class PermissionNameNotSpecified() : Error("permission.name.not_specified");
public class PermissionNameTooLong() : Error("permission.name.too_long");
public class PermissionParentNotFound() : Error("permission.parent.not_found");
public class PermissionNameDuplicate() : Error("permission.name.duplicate");
public class RoleNameNotSpecified() : Error("role.name.not_specified");
public class RoleNameTooLong() : Error("role.name.too_long");
public class RoleNotFound() : Error("role.not_found");
public class PermissionNotFound() : Error("permission.not_found");
public class RolePermissionNotFound() : Error("role.permission.not_found");
public class InvalidClaimStatus() : Error("role.permission.invalid_status");
