using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Features.Packages.Get;
using Pkgsrv.Application.Validation;
using Pkgsrv.Domain.Packages;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Application.Tests;

public sealed class GetPackageQueryHandlerTests
{
    [Fact]
    public async Task Get_AuthorizesReadAndReturnsPackage()
    {
        var repository = new TestRepository();
        var authorization = new TestAuthorization();
        var package = TestPackages.CreatePackage();
        await repository.TryAddAsync(package, CancellationToken.None);
        var handler = new GetPackageQueryHandler(repository, authorization);

        var result = await handler.Handle(
            new GetPackageQuery("widget", "1.2.3", TestPackages.Actor),
            CancellationToken.None);

        Assert.Equal("widget", result.Name);
        Assert.Equal("1.2.3", result.Version);
        Assert.Equal(PackageAction.Read, Assert.Single(authorization.Actions));
        Assert.Equal(package.Key, Assert.Single(authorization.Packages));
    }

    [Fact]
    public async Task Get_DeniesWhenReadIsNotAuthorized()
    {
        var repository = new TestRepository();
        var authorization = new TestAuthorization { Allow = false };
        await repository.TryAddAsync(TestPackages.CreatePackage(), CancellationToken.None);
        var handler = new GetPackageQueryHandler(repository, authorization);

        await Assert.ThrowsAsync<AuthorizationDeniedException>(
            async () => await handler.Handle(
                new GetPackageQuery("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    [Fact]
    public async Task Get_ReturnsNotFoundForUnknownPackage()
    {
        var handler = new GetPackageQueryHandler(
            new TestRepository(),
            new TestAuthorization());

        await Assert.ThrowsAsync<PackageNotFoundException>(
            async () => await handler.Handle(
                new GetPackageQuery("widget", "1.2.3", TestPackages.Actor),
                CancellationToken.None));
    }

    [Fact]
    public async Task Get_ReturnsValidationFailureBeforeAuthorization()
    {
        var authorization = new TestAuthorization();
        var handler = new GetPackageQueryHandler(new TestRepository(), authorization);

        var exception = await Assert.ThrowsAsync<ValidationFailedException>(
            async () => await handler.Handle(
                new GetPackageQuery("widget", "not-a-version", TestPackages.Actor),
                CancellationToken.None));

        Assert.Contains(exception.Failures, failure => failure.Field == "Version");
        Assert.Empty(authorization.Actions);
    }
}
