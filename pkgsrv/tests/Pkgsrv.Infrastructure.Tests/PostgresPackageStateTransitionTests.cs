using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Persistence.Postgres;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPackageStateTransitionTests
{
    [Theory]
    [InlineData(PackageState.Registered, PackageState.Registered)]
    [InlineData(PackageState.BuildRequested, PackageState.Registered)]
    public void ExpectedPersistedState_RequiresRegisteredBeforeForwardTransition(
        PackageState incomingState,
        PackageState expectedState)
    {
        Assert.Equal(
            expectedState,
            PostgresPackageStateTransition.ExpectedPersistedState(incomingState));
    }
}
