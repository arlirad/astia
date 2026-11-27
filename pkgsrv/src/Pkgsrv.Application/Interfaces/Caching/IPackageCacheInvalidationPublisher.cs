namespace Pkgsrv.Application.Interfaces.Caching;

public interface IPackageCacheInvalidationPublisher
{
    Task PublishAsync(
        PackageCacheInvalidation invalidation,
        CancellationToken cancellationToken);
}
