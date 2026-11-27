namespace Pkgsrv.Infrastructure.Events.Outbox;

public static class OutboxRetryPolicy
{
    public const int DefaultMaxAttempts = 10;

    public static bool ShouldRetry(int attemptCount, int maxAttempts) =>
        attemptCount < maxAttempts;

    public static TimeSpan Backoff(
        int attemptCount,
        TimeSpan baseBackoff,
        TimeSpan maxBackoff)
    {
        if (attemptCount <= 0)
        {
            return TimeSpan.Zero;
        }

        var multiplier = Math.Pow(2, Math.Min(attemptCount - 1, 31));
        var ticks = baseBackoff.Ticks * multiplier;

        if (double.IsNaN(ticks) || ticks >= maxBackoff.Ticks)
        {
            return maxBackoff;
        }

        return TimeSpan.FromTicks((long)ticks);
    }
}
