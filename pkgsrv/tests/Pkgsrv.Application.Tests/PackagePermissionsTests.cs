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
}
