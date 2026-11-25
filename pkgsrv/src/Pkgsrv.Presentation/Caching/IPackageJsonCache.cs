using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Presentation.Contracts;

namespace Pkgsrv.Presentation.Caching;

public interface IPackageJsonCache
{
    Task<string> GetListJsonAsync(
        AuthorizationContext actor,
        string? search,
        Func<CancellationToken, Task<PackageListResponse>> load,
        CancellationToken cancellationToken);

    Task<string> GetPackageJsonAsync(
        AuthorizationContext actor,
        string name,
        string version,
        Func<CancellationToken, Task<PackageResponse>> load,
        CancellationToken cancellationToken);
}
