using Irva.Core.Aggregates.Applications.Entities;
using Irva.Core.Aggregates.PermissionTrees;
using Irva.Core.Entities;
using Irva.Core.ValueObjects;

namespace Irva.Core.Aggregates.Applications;

public sealed class Application : Entity
{
    public string Name { get; private set; } = null!;
    public Guid PermissionTreeId
    {
        get; private set;
    }
    public DateTime? ArchivedAt
    {
        get; private set;
    }

    private readonly List<ApplicationToken> _tokens = [];
    public IReadOnlyList<ApplicationToken> Tokens => _tokens.AsReadOnly();

    public bool IsArchived => ArchivedAt.HasValue;

    public const int NameMaxLength = 128;

    private Application()
    {
    }

    public static Result<(Application App, PermissionTrees.PermissionTree Tree)> Create(string? name)
    {
        if (string.IsNullOrWhiteSpace(name))
            return Result<(Application, PermissionTrees.PermissionTree)>.Fail(new ApplicationNameNotSpecified());

        if (name.Length > NameMaxLength)
            return Result<(Application, PermissionTrees.PermissionTree)>.Fail(new ApplicationNameTooLong());

        var tree = PermissionTrees.PermissionTree.Create(name);
        if (!tree.IsSuccess)
            return Result<(Application, PermissionTrees.PermissionTree)>.Fail(tree.Error!);

        var app = new Application {
            Id = Guid.CreateVersion7(),
            Name = name,
            PermissionTreeId = tree.Value!.Id,
        };

        return Result<(Application App, PermissionTrees.PermissionTree Tree)>.Ok((app, tree.Value!));
    }

    public Result Rename(string? name)
    {
        if (IsArchived)
            return Result.Fail(new ApplicationIsArchived());

        if (string.IsNullOrWhiteSpace(name))
            return Result.Fail(new ApplicationNameNotSpecified());

        if (name.Length > NameMaxLength)
            return Result.Fail(new ApplicationNameTooLong());

        Name = name;
        return Result.Ok();
    }

    public void Archive()
    {
        if (IsArchived)
            return;
        ArchivedAt = DateTime.UtcNow;
    }

    public bool CanPermanentlyDelete => IsArchived;

    public Result<ApplicationToken> IssueToken(string? name, string token)
    {
        if (IsArchived)
            return Result<ApplicationToken>.Fail(new ApplicationIsArchived());

        if (string.IsNullOrWhiteSpace(name))
            return Result<ApplicationToken>.Fail(new ApplicationTokenNameNotSpecified());

        if (name.Length > NameMaxLength)
            return Result<ApplicationToken>.Fail(new ApplicationTokenNameTooLong());

        if (string.IsNullOrWhiteSpace(token))
            return Result<ApplicationToken>.Fail(new ApplicationTokenNotSpecified());

        var appToken = ApplicationToken.Create(Id, name, token);
        _tokens.Add(appToken);
        return Result<ApplicationToken>.Ok(appToken);
    }

    public Result RevokeToken(Guid tokenId)
    {
        var token = _tokens.FirstOrDefault(t => t.Id == tokenId);
        if (token is null)
            return Result.Fail(new ApplicationTokenNotFound());

        token.Revoke();
        return Result.Ok();
    }
}
