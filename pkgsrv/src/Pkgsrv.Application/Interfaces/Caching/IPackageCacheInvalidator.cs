namespace Pkgsrv.Application.Interfaces.Caching;

public interface IPackageCacheInvalidator
{
    void InvalidateAfterMutation();
}
