namespace Pkgsrv.Application.Interfaces.Caching;

public interface IPackageCacheInvalidator
{
    void Invalidate(PackageCacheInvalidation invalidation);
}
