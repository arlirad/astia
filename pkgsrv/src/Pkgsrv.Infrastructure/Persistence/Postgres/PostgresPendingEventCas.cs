using Pkgsrv.Application.Errors;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public static class PostgresPendingEventCas
{
    public const int MaxAttempts = 5;

    public static async Task ApplyAsync(
        PackageKey key,
        Func<CancellationToken, Task<string?>> loadPendingEventsJson,
        Func<string, string, CancellationToken, Task<bool>> tryReplace,
        IReadOnlyCollection<IDomainEvent> publishedEvents,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(key);
        ArgumentNullException.ThrowIfNull(loadPendingEventsJson);
        ArgumentNullException.ThrowIfNull(tryReplace);
        ArgumentNullException.ThrowIfNull(publishedEvents);

        for (var attempt = 0; attempt < MaxAttempts; attempt++)
        {
            var original = await loadPendingEventsJson(cancellationToken).ConfigureAwait(false);

            if (original is null)
            {
                throw new InvalidOperationException($"Package {key} does not exist in the store.");
            }

            var updated = PostgresPendingEventSerializer.RemoveAcknowledged(
                original,
                publishedEvents);

            if (string.Equals(original, updated, StringComparison.Ordinal))
            {
                return;
            }

            if (await tryReplace(original, updated, cancellationToken).ConfigureAwait(false))
            {
                return;
            }
        }

        throw new PackageConflictException(
            key.ToString(),
            $"Package {key} pending events changed concurrently; acknowledgement was not persisted.");
    }
}
