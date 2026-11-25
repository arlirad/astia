using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Interfaces.Security;

public interface IIrvaAuthorization
{
    Task<AuthorizationResult> AuthorizeAsync(
        AuthorizationContext context,
        PackageAction action,
        PackageKey? package,
        CancellationToken cancellationToken);
}
