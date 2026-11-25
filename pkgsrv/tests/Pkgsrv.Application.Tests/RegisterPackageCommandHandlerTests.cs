using Pkgsrv.Application.Abstractions.Security;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Features.Packages.Register;
using Pkgsrv.Application.Validation;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Application.Tests;

public sealed class RegisterPackageCommandHandlerTests
{
    [Fact]
    public async Task Register_PersistsPackageAndPublishesEvent()
    {
        var harness = new RegistrationHarness();

        var result = await harness.Handler.Handle(
            TestPackages.CreateRegistrationCommand(),
            CancellationToken.None);

        Assert.Equal("widget", result.Package.Name);
        Assert.Equal("1.2.3", result.Package.Version);
        Assert.Equal("Pending", result.Package.SignatureStatus);
        Assert.Equal(PackageState.Registered.ToString(), result.Package.State);
        Assert.Equal(PackageAction.Publish, Assert.Single(harness.Authorization.Actions));

        var stored = await harness.Repository.FindAsync(
            PackageKey.Create("widget", "1.2.3"),
            CancellationToken.None);
        Assert.NotNull(stored);
        Assert.IsType<PackageRegistered>(Assert.Single(harness.Events.Events));
    }

    [Fact]
    public async Task Register_AcceptsVerifiedSignatureMetadata()
    {
        var harness = new RegistrationHarness();

        var result = await harness.Handler.Handle(
            TestPackages.CreateRegistrationCommand(
                signature: "c2lnbmF0dXJlLXZhbHVl",
                signatureAlgorithm: "ed25519"),
            CancellationToken.None);

        Assert.Equal("Signed", result.Package.SignatureStatus);
        Assert.Equal("ed25519", result.Package.SignatureAlgorithm);
    }

    [Fact]
    public async Task Register_DeniesWhenPublishIsNotAuthorized()
    {
        var harness = new RegistrationHarness();
        harness.Authorization.Allow = false;

        var exception = await Assert.ThrowsAsync<AuthorizationDeniedException>(
            async () => await harness.Handler.Handle(
                TestPackages.CreateRegistrationCommand(),
                CancellationToken.None));

        Assert.Equal(nameof(PackageAction.Publish), exception.Action);
        Assert.Empty(await harness.Repository.SearchAsync(null, CancellationToken.None));
    }

    [Fact]
    public async Task Register_RejectsSourceOutsideAllowlist()
    {
        var harness = new RegistrationHarness();
        harness.Allowlist.Allowed = false;

        var exception = await Assert.ThrowsAsync<SourceNotAllowedException>(
            async () => await harness.Handler.Handle(
                TestPackages.CreateRegistrationCommand(),
                CancellationToken.None));

        Assert.Equal("github.com", exception.Host);
        Assert.Empty(await harness.Repository.SearchAsync(null, CancellationToken.None));
    }

    [Fact]
    public async Task Register_RejectsDuplicatePackageVersion()
    {
        var harness = new RegistrationHarness();
        var command = TestPackages.CreateRegistrationCommand();

        await harness.Handler.Handle(command, CancellationToken.None);

        await Assert.ThrowsAsync<PackageConflictException>(
            async () => await harness.Handler.Handle(command, CancellationToken.None));
    }

    [Fact]
    public async Task Register_ReturnsValidationFailuresBeforeAuthorization()
    {
        var harness = new RegistrationHarness();

        var exception = await Assert.ThrowsAsync<ValidationFailedException>(
            async () => await harness.Handler.Handle(
                TestPackages.CreateRegistrationCommand(name: null),
                CancellationToken.None));

        Assert.Contains(exception.Failures, failure => failure.Field == "Name");
        Assert.Empty(harness.Authorization.Actions);
    }

    [Fact]
    public void RegistrationCommand_ExposesNoShellOrCommandField()
    {
        var propertyNames = typeof(RegisterPackageCommand)
            .GetProperties()
            .Select(property => property.Name)
            .ToArray();

        Assert.DoesNotContain(propertyNames, name => name.Contains("Command", StringComparison.OrdinalIgnoreCase));
        Assert.DoesNotContain(propertyNames, name => name.Contains("Shell", StringComparison.OrdinalIgnoreCase));
        Assert.DoesNotContain(propertyNames, name => name.Contains("Script", StringComparison.OrdinalIgnoreCase));
        Assert.DoesNotContain(propertyNames, name => name.Contains("Executable", StringComparison.OrdinalIgnoreCase));
    }

    private sealed class RegistrationHarness
    {
        public TestRepository Repository { get; } = new();

        public TestAuthorization Authorization { get; } = new();

        public TestSourceAllowlist Allowlist { get; } = new();

        public TestEventPublisher Events { get; } = new();

        public TestClock Clock { get; } = new();

        public RegisterPackageCommandHandler Handler => new(
            Repository,
            Allowlist,
            Authorization,
            Events,
            Clock);
    }
}
