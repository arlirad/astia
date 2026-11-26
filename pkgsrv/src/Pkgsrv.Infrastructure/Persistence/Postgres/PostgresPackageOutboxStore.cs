using Microsoft.EntityFrameworkCore;
using Pkgsrv.Infrastructure.Events.Outbox;

namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public sealed class PostgresPackageOutboxStore : IPackageOutboxStore
{
    private readonly IDbContextFactory<PostgresPackageDbContext> _contextFactory;

    public PostgresPackageOutboxStore(
        IDbContextFactory<PostgresPackageDbContext> contextFactory)
    {
        _contextFactory = contextFactory;
    }

    public async Task<IReadOnlyList<PackageOutboxMessage>> ClaimAsync(
        DateTimeOffset now,
        int maxCount,
        TimeSpan lease,
        CancellationToken cancellationToken)
    {
        if (maxCount <= 0)
        {
            return Array.Empty<PackageOutboxMessage>();
        }

        var pendingStatus = (int)PackageOutboxStatus.Pending;
        var leaseThreshold = now - lease;
        var claimId = Guid.NewGuid().ToString("N");

        await using var context = await _contextFactory
            .CreateDbContextAsync(cancellationToken)
            .ConfigureAwait(false);
        await using var transaction = await context.Database
            .BeginTransactionAsync(cancellationToken)
            .ConfigureAwait(false);

        var records = await context.PackageOutbox
            .FromSqlInterpolated(
                $"""
                SELECT * FROM package_outbox
                WHERE "Status" = {pendingStatus}
                  AND "AvailableAt" <= {now}
                  AND ("ClaimedAt" IS NULL OR "ClaimedAt" < {leaseThreshold})
                ORDER BY "AvailableAt", "OccurredAt", "IdempotencyKey"
                LIMIT {maxCount}
                FOR UPDATE SKIP LOCKED
                """)
            .ToListAsync(cancellationToken)
            .ConfigureAwait(false);

        foreach (var record in records)
        {
            record.ClaimedAt = now;
            record.ClaimId = claimId;
            record.AttemptCount += 1;
        }

        await context.SaveChangesAsync(cancellationToken).ConfigureAwait(false);
        await transaction.CommitAsync(cancellationToken).ConfigureAwait(false);

        return records
            .Select(record => new PackageOutboxMessage(
                record.IdempotencyKey,
                record.PackageName,
                record.PackageVersion,
                record.EventType,
                record.Payload,
                record.OccurredAt,
                record.AttemptCount,
                claimId))
            .ToArray();
    }

    public async Task MarkDeliveredAsync(
        string idempotencyKey,
        string claimId,
        CancellationToken cancellationToken)
    {
        var pendingStatus = (int)PackageOutboxStatus.Pending;
        var deliveredStatus = (int)PackageOutboxStatus.Delivered;

        await using var context = await _contextFactory
            .CreateDbContextAsync(cancellationToken)
            .ConfigureAwait(false);

        await context.PackageOutbox
            .Where(record => record.IdempotencyKey == idempotencyKey
                && record.ClaimId == claimId
                && record.Status == pendingStatus)
            .ExecuteUpdateAsync(
                setters => setters
                    .SetProperty(record => record.Status, deliveredStatus)
                    .SetProperty(record => record.ClaimedAt, (DateTimeOffset?)null)
                    .SetProperty(record => record.ClaimId, (string?)null)
                    .SetProperty(record => record.LastError, (string?)null),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async Task MarkFailedAsync(
        string idempotencyKey,
        string claimId,
        int attemptCount,
        DateTimeOffset nextAvailableAt,
        bool exhausted,
        string error,
        CancellationToken cancellationToken)
    {
        var pendingStatus = (int)PackageOutboxStatus.Pending;
        var status = exhausted
            ? (int)PackageOutboxStatus.Failed
            : pendingStatus;

        await using var context = await _contextFactory
            .CreateDbContextAsync(cancellationToken)
            .ConfigureAwait(false);

        await context.PackageOutbox
            .Where(record => record.IdempotencyKey == idempotencyKey
                && record.ClaimId == claimId
                && record.Status == pendingStatus
                && record.AttemptCount == attemptCount)
            .ExecuteUpdateAsync(
                setters => setters
                    .SetProperty(record => record.Status, status)
                    .SetProperty(record => record.ClaimedAt, (DateTimeOffset?)null)
                    .SetProperty(record => record.ClaimId, (string?)null)
                    .SetProperty(record => record.AvailableAt, nextAvailableAt)
                    .SetProperty(record => record.LastError, error),
                cancellationToken)
            .ConfigureAwait(false);
    }
}
