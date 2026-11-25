using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Interfaces.Sources;

public interface ISourceAllowlist
{
    bool IsAllowed(PackageSource source);
}
