using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Abstractions.Security;

public interface IIrvaAuthorization
{
    Task<AuthorizationResult> AuthorizeAsync(
        AuthorizationContext context,
        PackageAction action,
        PackageKey? package,
        CancellationToken cancellationToken);
}
