using Microsoft.EntityFrameworkCore;
using Pkgsrv.Domain.Events;

namespace Pkgsrv.Infrastructure.Persistence.Postgres;

internal static class PostgresPackageOutboxWriter
{
    public static async Task WriteAsync(
        PostgresPackageDbContext context,
        IReadOnlyCollection<IDomainEvent> domainEvents,
        DateTimeOffset availableAt,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(domainEvents);

        foreach (var domainEvent in domainEvents)
        {
            var outboxEvent = PostgresOutboxEventSerializer.ToOutboxEvent(domainEvent);

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
    }
}
