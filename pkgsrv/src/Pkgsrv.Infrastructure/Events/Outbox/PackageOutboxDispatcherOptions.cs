namespace Pkgsrv.Infrastructure.Events.Outbox;

public sealed record PackageOutboxDispatcherOptions
{
    public int BatchSize { get; init; } = 32;

    public TimeSpan PollInterval { get; init; } = TimeSpan.FromSeconds(1);

    public TimeSpan ErrorBackoff { get; init; } = TimeSpan.FromSeconds(5);

    public TimeSpan LeaseDuration { get; init; } = TimeSpan.FromMinutes(5);

    public int MaxAttempts { get; init; } = OutboxRetryPolicy.DefaultMaxAttempts;

    public TimeSpan BaseBackoff { get; init; } = TimeSpan.FromSeconds(5);

    public TimeSpan MaxBackoff { get; init; } = TimeSpan.FromMinutes(10);

    public static PackageOutboxDispatcherOptions Default { get; } = new();
}
