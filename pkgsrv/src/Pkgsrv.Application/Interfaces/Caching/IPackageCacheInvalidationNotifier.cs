namespace Pkgsrv.Application.Interfaces.Caching;

public interface IPackageCacheInvalidationNotifier
{
    Task NotifyAsync(
        PackageCacheInvalidation invalidation,
        CancellationToken cancellationToken);
}
