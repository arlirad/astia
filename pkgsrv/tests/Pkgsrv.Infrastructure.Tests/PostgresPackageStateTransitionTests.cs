using Pkgsrv.Domain.Errors;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Postgres;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPackageStateTransitionTests
{
    [Theory]
    [InlineData(PackageState.Registered, PackageState.Registered)]
    [InlineData(PackageState.Registered, PackageState.BuildRequested)]
    public void EnsureAllowed_PermitsForwardOrUnchangedTransitions(
        PackageState persistedState,
        PackageState incomingState)
    {
        PostgresPackageStateTransition.EnsureAllowed(persistedState, incomingState);
    }

    [Fact]
    public void EnsureAllowed_RejectsDuplicateBuildRequest()
    {
        var exception = Assert.Throws<DomainException>(
            () => PostgresPackageStateTransition.EnsureAllowed(
                PackageState.BuildRequested,
                PackageState.BuildRequested));

        Assert.Equal(DomainErrorCodes.BuildAlreadyRequested, exception.Code);
    }

    [Fact]
    public void EnsureAllowed_RejectsStaleDowngrade()
    {
        var exception = Assert.Throws<DomainException>(
            () => PostgresPackageStateTransition.EnsureAllowed(
                PackageState.BuildRequested,
                PackageState.Registered));

        Assert.Equal(DomainErrorCodes.PackageStateConflict, exception.Code);
    }
}
