using Microsoft.EntityFrameworkCore;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Persistence.Postgres;

internal static class PostgresPackageOutboxWriter
{
    public static async Task WriteAsync(
        PostgresPackageDbContext context,
        PackageKey key,
        IReadOnlyCollection<IDomainEvent> domainEvents,
        DateTimeOffset availableAt,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(key);
        ArgumentNullException.ThrowIfNull(domainEvents);

        var hasSupportedEvent = false;

        foreach (var domainEvent in domainEvents)
        {
            var outboxEvent = PostgresOutboxEventSerializer.ToOutboxEvent(domainEvent);
            hasSupportedEvent = true;

            await context.Database
                .ExecuteSqlInterpolatedAsync(
                    $"""
                    INSERT INTO package_outbox (
                        "IdempotencyKey", "PackageName", "PackageVersion", "EventType",
                        "Payload", "OccurredAt", "AvailableAt", "ClaimedAt", "ClaimId",
                        "AttemptCount", "Status", "LastError"
                    )
                    VALUES (
                        {outboxEvent.IdempotencyKey}, {outboxEvent.PackageName},
                        {outboxEvent.PackageVersion}, {outboxEvent.EventType},
                        {outboxEvent.Payload}, {outboxEvent.OccurredAt}, {availableAt},
                        NULL, NULL, 0, 0, NULL
                    )
                    ON CONFLICT ("IdempotencyKey") DO NOTHING
                    """,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        if (hasSupportedEvent)
        {
            await ClearPendingEventsAsync(context, key, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private static Task<int> ClearPendingEventsAsync(
        PostgresPackageDbContext context,
        PackageKey key,
        CancellationToken cancellationToken) =>
        context.Database.ExecuteSqlInterpolatedAsync(
            $"""
            UPDATE packages
            SET "PendingEventsJson" = '[]'
            WHERE "Name" = {key.Name.Value}
              AND "Version" = {key.Version.ToString()}
            """,
            cancellationToken);
}
