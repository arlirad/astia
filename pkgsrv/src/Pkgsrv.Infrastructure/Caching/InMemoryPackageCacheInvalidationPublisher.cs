using Pkgsrv.Application.Interfaces.Caching;

namespace Pkgsrv.Infrastructure.Caching;

public sealed class InMemoryPackageCacheInvalidationPublisher
    : IPackageCacheInvalidationPublisher
{
    public Task PublishAsync(
        PackageCacheInvalidation invalidation,
        CancellationToken cancellationToken) =>
        Task.CompletedTask;
}
