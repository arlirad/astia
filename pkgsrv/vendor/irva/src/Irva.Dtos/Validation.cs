using Irva.Core.Aggregates.Accounts;
using Irva.Core.Aggregates.Applications;
using Irva.Core.Aggregates.PermissionTrees;
using Irva.Core.Aggregates.PermissionTrees.Entities;

namespace Irva.Dtos;

public static class RequestValidation
{
    private const int PasswordMinLength = 8;

    private static bool TryValidateUsername(string username)
    {
        return string.IsNullOrWhiteSpace(username)
            ? false
            : username.Length is not < 3 and not > Account.UsernameMaxLength && char.IsLetter(username[0]) && username.All(c => char.IsLetterOrDigit(c) || c == '-' || c == '_');
    }

    public static string? Validate(CreateAccountRequest req)
    {
        if (string.IsNullOrWhiteSpace(req.Username))
            return "username: required";
        if (string.IsNullOrWhiteSpace(req.Password))
            return "password: required";
        if (req.Password.Length < PasswordMinLength)
            return $"password: must be at least {PasswordMinLength} characters";
        return req.Username.Length > Account.UsernameMaxLength
            ? $"username: must not exceed {Account.UsernameMaxLength} characters"
            : !TryValidateUsername(req.Username)
            ? "username: invalid format"
            : req.Password.Length > Account.AuthenticationDataMaxLength
            ? $"password: must not exceed {Account.AuthenticationDataMaxLength} characters"
            : null;
    }

    public static string? Validate(CreateApplicationRequest req)
    {
        return string.IsNullOrWhiteSpace(req.Name)
            ? "name: required"
            : req.Name.Length > Irva.Core.Aggregates.Applications.Application.NameMaxLength
            ? $"name: must not exceed {Irva.Core.Aggregates.Applications.Application.NameMaxLength} characters"
            : null;
    }

    public static string? Validate(RenameApplicationRequest req)
    {
        return string.IsNullOrWhiteSpace(req.Name)
            ? "name: required"
            : req.Name.Length > Irva.Core.Aggregates.Applications.Application.NameMaxLength
            ? $"name: must not exceed {Irva.Core.Aggregates.Applications.Application.NameMaxLength} characters"
            : null;
    }

    public static string? Validate(CreateApplicationTokenRequest req)
    {
        return string.IsNullOrWhiteSpace(req.Name)
            ? "name: required"
            : req.Name.Length > Irva.Core.Aggregates.Applications.Application.NameMaxLength
            ? $"name: must not exceed {Irva.Core.Aggregates.Applications.Application.NameMaxLength} characters"
            : null;
    }

    public static string? Validate(AddPermissionRequest req)
    {
        return string.IsNullOrWhiteSpace(req.Name)
            ? "name: required"
            : req.Name.Length > PermissionTree.NameMaxLength ? $"name: must not exceed {PermissionTree.NameMaxLength} characters" : null;
    }

    public static string? Validate(CreateRoleRequest req)
    {
        return string.IsNullOrWhiteSpace(req.Name)
            ? "name: required"
            : req.Name.Length > Role.NameMaxLength ? $"name: must not exceed {Role.NameMaxLength} characters" : null;
    }

    public static string? Validate(SetRolePermissionRequest req) => !Enum.IsDefined(req.Status) ? $"status: must be 0 (Inherit), 1 (Permit), or 2 (Disallow)" : null;

    public static string? Validate(ChangeUsernameRequest req)
    {
        return string.IsNullOrWhiteSpace(req.Username)
            ? "username: required"
            : req.Username.Length > Account.UsernameMaxLength
            ? $"username: must not exceed {Account.UsernameMaxLength} characters"
            : !TryValidateUsername(req.Username) ? "username: invalid format" : null;
    }

    public static string? Validate(ChangePasswordRequest req)
    {
        if (string.IsNullOrWhiteSpace(req.OldPassword))
            return "old_password: required";
        return string.IsNullOrWhiteSpace(req.NewPassword)
            ? "new_password: required"
            : req.NewPassword.Length < PasswordMinLength
            ? $"new_password: must be at least {PasswordMinLength} characters"
            : req.NewPassword.Length > Account.AuthenticationDataMaxLength
            ? $"new_password: must not exceed {Account.AuthenticationDataMaxLength} characters"
            : null;
    }

    public static string? Validate(AttestRequest req)
    {
        return string.IsNullOrWhiteSpace(req.Username)
            ? "username: required"
            : string.IsNullOrWhiteSpace(req.Password) ? "password: required" : null;
    }

    public static string? Validate(RefreshRequest req) => string.IsNullOrWhiteSpace(req.RefreshToken) ? "refresh_token: required" : null;

    public static string? Validate(RevokeRequest req) => string.IsNullOrWhiteSpace(req.RefreshToken) ? "refresh_token: required" : null;

    public static string? Validate(VerifyTokenRequest req) => string.IsNullOrWhiteSpace(req.AccessToken) ? "access_token: required" : null;
}