using Arlirad.Ervi.Mediator;
using Arlirad.Ervi.Mediator.Interfaces;
using Microsoft.Extensions.DependencyInjection;
using Pkgsrv.Application.Abstractions.Events;
using Pkgsrv.Application.Abstractions.Persistence;
using Pkgsrv.Application.Abstractions.Security;
using Pkgsrv.Application.Abstractions.Sources;
using Pkgsrv.Application.Abstractions.Time;
using Pkgsrv.Application.Features.Packages.Register;
using Pkgsrv.Domain.Packages;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Application.Tests;

public sealed class ErviMediatorIntegrationTests
{
    [Fact]
    public async Task AddMediator_DispatchesRegistrationThroughErviContracts()
    {
        var repository = new TestRepository();
        var services = new ServiceCollection();
        services.AddSingleton<IPackageRepository>(repository);
        services.AddSingleton<IPackageIndex>(repository);
        services.AddSingleton<ISourceAllowlist>(new TestSourceAllowlist());
        services.AddSingleton<IIrvaAuthorization>(new TestAuthorization());
        services.AddSingleton<IDomainEventPublisher>(new TestEventPublisher());
        services.AddSingleton<ISystemClock>(new TestClock());
        services.AddMediator(typeof(RegisterPackageCommandHandler));

        await using var provider = services.BuildServiceProvider();
        var mediator = provider.GetRequiredService<IMediator>();

        var result = await mediator.Send(
            TestPackages.CreateRegistrationCommand(),
            CancellationToken.None);

        Assert.Equal("widget", result.Package.Name);
        Assert.NotNull(await repository.FindAsync(
            PackageKey.Create("widget", "1.2.3"),
            CancellationToken.None));
    }
}
