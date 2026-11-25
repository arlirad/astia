using Pkgsrv.Infrastructure.Persistence.Postgres;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresLikePatternTests
{
    [Fact]
    public void Contains_EscapesPercentUnderscoreAndBackslash()
    {
        Assert.Equal(
            @"%50\%\_off\\path%",
            PostgresLikePattern.Contains(@"50%_off\path"));
    }

    [Fact]
    public void Prefix_EscapesPercentUnderscoreAndBackslash()
    {
        Assert.Equal(
            @"50\%\_off\\path%",
            PostgresLikePattern.Prefix(@"50%_off\path"));
    }
}
