namespace Pkgsrv.Infrastructure.Events.Outbox;

public enum PackageOutboxStatus
{
    Pending = 0,
    Delivered = 1,
    Failed = 2
}

public sealed record PackageOutboxMessage(
    string IdempotencyKey,
    string PackageName,
    string PackageVersion,
    string EventType,
    string Payload,
    DateTimeOffset OccurredAt,
    int AttemptCount,
    string ClaimId);

public interface IPackageOutboxStore
{
    Task<IReadOnlyList<PackageOutboxMessage>> ClaimAsync(
        DateTimeOffset now,
        int maxCount,
        TimeSpan lease,
        CancellationToken cancellationToken);

    Task MarkDeliveredAsync(
        string idempotencyKey,
        string claimId,
        CancellationToken cancellationToken);

    Task MarkFailedAsync(
        string idempotencyKey,
        string claimId,
        int attemptCount,
        DateTimeOffset nextAvailableAt,
        bool exhausted,
        string error,
        CancellationToken cancellationToken);
}
