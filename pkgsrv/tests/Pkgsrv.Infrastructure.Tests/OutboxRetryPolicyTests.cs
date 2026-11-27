using Pkgsrv.Infrastructure.Events.Outbox;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class OutboxRetryPolicyTests
{
    [Fact]
    public void Backoff_UsesBoundedExponentialGrowth()
    {
        var baseBackoff = TimeSpan.FromSeconds(5);
        var maxBackoff = TimeSpan.FromMinutes(1);

        Assert.Equal(baseBackoff, OutboxRetryPolicy.Backoff(1, baseBackoff, maxBackoff));
        Assert.Equal(
            TimeSpan.FromSeconds(10),
            OutboxRetryPolicy.Backoff(2, baseBackoff, maxBackoff));
        Assert.Equal(
            TimeSpan.FromSeconds(20),
            OutboxRetryPolicy.Backoff(3, baseBackoff, maxBackoff));
        Assert.Equal(maxBackoff, OutboxRetryPolicy.Backoff(20, baseBackoff, maxBackoff));
    }

    [Fact]
    public void ShouldRetry_StopsAtMaximumAttempts()
    {
        Assert.True(OutboxRetryPolicy.ShouldRetry(1, 3));
        Assert.True(OutboxRetryPolicy.ShouldRetry(2, 3));
        Assert.False(OutboxRetryPolicy.ShouldRetry(3, 3));
        Assert.False(OutboxRetryPolicy.ShouldRetry(4, 3));
    }
}
