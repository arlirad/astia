using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Abstractions.Sources;

public interface ISourceAllowlist
{
    bool IsAllowed(PackageSource source);
}
