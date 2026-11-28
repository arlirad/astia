using Irva.Core.Aggregates.Accounts.Entities;
using Irva.Core.Aggregates.Accounts.Events;
using Irva.Core.Entities;
using Irva.Core.ValueObjects;

namespace Irva.Core.Aggregates.Accounts;

public sealed class Account : Entity
{
    public string Username { get; private set; } = null!;
    public string AuthenticationData { get; private set; } = null!;

    private readonly List<AccountRoleClaim> _roleClaims = [];
    public IReadOnlyList<AccountRoleClaim> RoleClaims => _roleClaims.AsReadOnly();

    private readonly List<AccountTreeOversee> _treeOversees = [];
    public IReadOnlyList<AccountTreeOversee> TreeOversees => _treeOversees.AsReadOnly();

    public const int UsernameMaxLength = 64;
    public const int AuthenticationDataMaxLength = 256;

    private static bool IsValidUsernameFormat(string username) => !string.IsNullOrWhiteSpace(username) && username.Length is not < 3 and not > UsernameMaxLength && char.IsLetter(username[0]) && username.All(c => char.IsLetterOrDigit(c) || c == '-' || c == '_');

    public static Result<Account> Create(string? username, string? authenticationData)
    {
        if (string.IsNullOrWhiteSpace(username))
            return Result<Account>.Fail(new UsernameNotSpecified());

        return username.Length > UsernameMaxLength
            ? Result<Account>.Fail(new UsernameTooLong())
            : !IsValidUsernameFormat(username)
            ? Result<Account>.Fail(new UsernameInvalidFormat())
            : string.IsNullOrWhiteSpace(authenticationData)
            ? Result<Account>.Fail(new AuthenticationDataNotSpecified())
            : authenticationData.Length > AuthenticationDataMaxLength
            ? Result<Account>.Fail(new AuthenticationDataTooLong())
            : Result<Account>.Ok(new Account {
                Id = Guid.CreateVersion7(),
                Username = username.ToLowerInvariant(),
                AuthenticationData = authenticationData,
            });
    }

    public AccountRoleClaim GrantRole(Guid roleId)
    {
        var existing = _roleClaims.FirstOrDefault(c => c.RoleId == roleId);
        if (existing is not null)
            return existing;

        var claim = new AccountRoleClaim(Id, roleId);
        _roleClaims.Add(claim);
        return claim;
    }

    public void RevokeRole(Guid roleId) => _roleClaims.RemoveAll(c => c.RoleId == roleId);

    public AccountTreeOversee GrantTreeOversee(Guid permissionTreeId)
    {
        var existing = _treeOversees.FirstOrDefault(o => o.PermissionTreeId == permissionTreeId);
        if (existing is not null)
            return existing;

        var oversee = new AccountTreeOversee(Id, permissionTreeId);
        _treeOversees.Add(oversee);
        return oversee;
    }

    public void RevokeTreeOversee(Guid permissionTreeId) => _treeOversees.RemoveAll(o => o.PermissionTreeId == permissionTreeId);

    public Result ChangeUsername(string? username)
    {
        if (string.IsNullOrWhiteSpace(username))
            return Result.Fail(new UsernameNotSpecified());

        if (!IsValidUsernameFormat(username))
            return Result.Fail(new UsernameInvalidFormat());

        if (username.Length > UsernameMaxLength)
            return Result.Fail(new UsernameTooLong());

        Username = username.ToLowerInvariant();
        return Result.Ok();
    }

    public Result ChangePassword(string authenticationData)
    {
        if (string.IsNullOrWhiteSpace(authenticationData))
            return Result.Fail(new AuthenticationDataNotSpecified());

        if (authenticationData.Length > AuthenticationDataMaxLength)
            return Result.Fail(new AuthenticationDataTooLong());

        AuthenticationData = authenticationData;
        return Result.Ok();
    }

    public void Delete() => Publish(new Events.AccountDeleted(Id));

    public void ClearRoleClaims() => _roleClaims.Clear();

    public void ClearTreeOversees() => _treeOversees.Clear();
}