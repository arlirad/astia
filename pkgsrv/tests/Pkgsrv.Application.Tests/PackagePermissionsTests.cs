using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Authorization;

namespace Pkgsrv.Application.Tests;

public sealed class PackagePermissionsTests
{
    [Theory]
    [InlineData(PackageAction.Read, PackagePermissions.Read)]
    [InlineData(PackageAction.Build, PackagePermissions.Build)]
    [InlineData(PackageAction.Publish, PackagePermissions.Publish)]
    public void For_MapsApplicationActionsToIrvaPermissionNames(
        PackageAction action,
        string expectedPermission)
    {
        Assert.Equal(expectedPermission, PackagePermissions.For(action));
    }

    [Fact]
    public void PermissionNames_MatchDeployedErvanConvention()
    {
        Assert.Equal("read", PackagePermissions.Read);
        Assert.Equal("build", PackagePermissions.Build);
        Assert.Equal("publish", PackagePermissions.Publish);
    }

    [Fact]
    public void EnsureAuthorized_AllowsVerifiedContext()
    {
        var verified = AuthorizationContext.Create(
            "alice",
            accountId: Guid.Parse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"));

        AuthorizationResult.Allow(verified, "verified")
            .EnsureAuthorized(nameof(PackageAction.Read));
    }

    [Fact]
    public void EnsureAuthorized_MapsAuthenticationRequired()
    {
        var exception = Assert.Throws<AuthenticationRequiredException>(
            () => AuthorizationResult.AuthenticationRequired("missing bearer")
                .EnsureAuthorized(nameof(PackageAction.Read)));

        Assert.Equal("authorization.unauthenticated", exception.Code);
    }

    [Fact]
    public void EnsureAuthorized_MapsPermissionDenial()
    {
        var exception = Assert.Throws<AuthorizationDeniedException>(
            () => AuthorizationResult.Deny("no permission")
                .EnsureAuthorized(nameof(PackageAction.Publish)));

        Assert.Equal("authorization.denied", exception.Code);
    }

    [Fact]
    public void EnsureAuthorized_MapsUnavailable()
    {
        var exception = Assert.Throws<AuthorizationUnavailableException>(
            () => AuthorizationResult.Unavailable("Irva unreachable")
                .EnsureAuthorized(nameof(PackageAction.Build)));

        Assert.Equal("authorization.unavailable", exception.Code);
    }
}
