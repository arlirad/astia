using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Features.Packages.List;
using Pkgsrv.Domain.Packages;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Application.Tests;

public sealed class ListPackagesQueryHandlerTests
{
    [Fact]
    public async Task List_AuthorizesReadAndReturnsIndexedPackages()
    {
        var repository = new TestRepository();
        var authorization = new TestAuthorization();
        await repository.TryAddAsync(TestPackages.CreatePackage(), CancellationToken.None);
        var handler = new ListPackagesQueryHandler(repository, authorization);

        var packages = await handler.Handle(
            new ListPackagesQuery(null, TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal(PackageAction.Read, Assert.Single(authorization.Actions));
        Assert.Equal("widget", Assert.Single(packages).Name);
    }

    [Fact]
    public async Task List_DeniesWhenReadIsNotAuthorized()
    {
        var repository = new TestRepository();
        var authorization = new TestAuthorization { Allow = false };
        var handler = new ListPackagesQueryHandler(repository, authorization);

        await Assert.ThrowsAsync<AuthorizationDeniedException>(
            async () => await handler.Handle(
                new ListPackagesQuery(null, TestPackages.Actor),
                CancellationToken.None));

        Assert.Equal(PackageAction.Read, Assert.Single(authorization.Actions));
    }

    [Fact]
    public async Task List_PassesSearchToIndex()
    {
        var repository = new TestRepository();
        var authorization = new TestAuthorization();
        await repository.TryAddAsync(TestPackages.CreatePackage(name: "widget"), CancellationToken.None);
        await repository.TryAddAsync(TestPackages.CreatePackage(name: "libc"), CancellationToken.None);
        var handler = new ListPackagesQueryHandler(repository, authorization);

        var packages = await handler.Handle(
            new ListPackagesQuery("libc", TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal("libc", Assert.Single(packages).Name);
    }
}
