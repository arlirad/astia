using Microsoft.Extensions.Logging;
using Pkgsrv.Application.Interfaces.Caching;

namespace Pkgsrv.Presentation.Caching;

public sealed class PackageCacheInvalidationNotifier
    : IPackageCacheInvalidationNotifier
{
    private readonly IPackageCacheInvalidator _localInvalidator;
    private readonly IPackageCacheInvalidationPublisher _publisher;
    private readonly ILogger<PackageCacheInvalidationNotifier> _logger;

    public PackageCacheInvalidationNotifier(
        IPackageCacheInvalidator localInvalidator,
        IPackageCacheInvalidationPublisher publisher,
        ILogger<PackageCacheInvalidationNotifier> logger)
    {
        _localInvalidator = localInvalidator;
        _publisher = publisher;
        _logger = logger;
    }

    public async Task NotifyAsync(
        PackageCacheInvalidation invalidation,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(invalidation);

        try
        {
            _localInvalidator.Invalidate(invalidation);
        }
        catch (Exception exception)
        {
            _logger.LogWarning(
                "Local package cache invalidation failed for scope {Scope} ({ErrorType}).",
                invalidation.Scope,
                exception.GetType().Name);
        }

        try
        {
            await _publisher
                .PublishAsync(invalidation, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (Exception exception)
        {
            _logger.LogWarning(
                "Package cache invalidation publication failed for scope {Scope} ({ErrorType}).",
                invalidation.Scope,
                exception.GetType().Name);
        }
    }
}
