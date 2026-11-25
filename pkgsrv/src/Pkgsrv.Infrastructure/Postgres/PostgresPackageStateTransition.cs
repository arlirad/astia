using Pkgsrv.Domain.Errors;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Postgres;

public static class PostgresPackageStateTransition
{
    public static void EnsureAllowed(PackageState persistedState, PackageState incomingState)
    {
        if (persistedState == PackageState.BuildRequested
            && incomingState == PackageState.Registered)
        {
            throw new DomainException(
                DomainErrorCodes.PackageStateConflict,
                "Persisted package state cannot be downgraded from BuildRequested.");
        }

        if (persistedState == PackageState.BuildRequested
            && incomingState == PackageState.BuildRequested)
        {
            throw new DomainException(
                DomainErrorCodes.BuildAlreadyRequested,
                "A build has already been requested for this package version.");
        }
    }
}
