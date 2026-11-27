namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public sealed class PackageOutboxRecord
{
    public string IdempotencyKey { get; set; } = string.Empty;

    public string PackageName { get; set; } = string.Empty;

    public string PackageVersion { get; set; } = string.Empty;

    public string EventType { get; set; } = string.Empty;

    public string Payload { get; set; } = string.Empty;

    public DateTimeOffset OccurredAt { get; set; }

    public DateTimeOffset AvailableAt { get; set; }

    public DateTimeOffset? ClaimedAt { get; set; }

    public string? ClaimId { get; set; }

    public int AttemptCount { get; set; }

    public int Status { get; set; }

    public string? LastError { get; set; }
}
