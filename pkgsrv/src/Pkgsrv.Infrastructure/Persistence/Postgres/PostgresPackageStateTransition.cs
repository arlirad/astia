using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public static class PostgresPackageStateTransition
{
    public static PackageState ExpectedPersistedState(PackageState incomingState) =>
        incomingState == PackageState.BuildRequested
            ? PackageState.Registered
            : incomingState;
}
