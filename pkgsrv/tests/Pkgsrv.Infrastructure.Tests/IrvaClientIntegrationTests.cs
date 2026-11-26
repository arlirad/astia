using Pkgsrv.Application.Authorization;
using Pkgsrv.Infrastructure.Security;

namespace Pkgsrv.Infrastructure.Tests;

[AttributeUsage(AttributeTargets.Method)]
public sealed class IrvaIntegrationFactAttribute : FactAttribute
{
    public IrvaIntegrationFactAttribute()
    {
        if (string.IsNullOrWhiteSpace(
                Environment.GetEnvironmentVariable(IrvaClientIntegrationTests.BaseUrlVariable))
            || string.IsNullOrWhiteSpace(
                Environment.GetEnvironmentVariable(IrvaClientIntegrationTests.AppTokenVariable))
            || string.IsNullOrWhiteSpace(
                Environment.GetEnvironmentVariable(IrvaClientIntegrationTests.AccessTokenVariable)))
        {
            Skip = $"Set {IrvaClientIntegrationTests.BaseUrlVariable}, {IrvaClientIntegrationTests.AppTokenVariable}, and {IrvaClientIntegrationTests.AccessTokenVariable} to run live Irva integration tests.";
        }
    }
}

public sealed class IrvaClientIntegrationTests
{
    public const string BaseUrlVariable = "PKGSRV_TEST_IRVA_BASE_URL";
    public const string AppTokenVariable = "PKGSRV_TEST_IRVA_APP_TOKEN";
    public const string AccessTokenVariable = "PKGSRV_TEST_IRVA_ACCESS_TOKEN";
    public const string SocketAddressVariable = "PKGSRV_TEST_IRVA_SOCKET_ADDRESS";

    [IrvaIntegrationFact]
    [Trait("Category", "IrvaIntegration")]
    public async Task VerifyAsync_AgainstLiveIrva_ReturnsVerifiedAccount()
    {
        if (!IrvaClientAdapterFactory.IsAvailable)
        {
            throw new InvalidOperationException(
                "Build the tests with -p:IrvaClientProject=<path-to-Irva.Client.csproj> to run live Irva integration tests.");
        }

        var settings = new IrvaClientConnectionSettings(
            Environment.GetEnvironmentVariable(BaseUrlVariable)!,
            Environment.GetEnvironmentVariable(AppTokenVariable)!,
            Environment.GetEnvironmentVariable(SocketAddressVariable));
        var adapter = IrvaClientAdapterFactory.Create(settings);

        try
        {
            var account = await adapter.VerifyAsync(
                Environment.GetEnvironmentVariable(AccessTokenVariable)!,
                CancellationToken.None);

            Assert.NotEqual(Guid.Empty, account.AccountId);
            Assert.False(string.IsNullOrWhiteSpace(account.Username));
            _ = adapter.HasPermission(account.AccountId, PackagePermissions.Read);
        }
        finally
        {
            (adapter as IDisposable)?.Dispose();
        }
    }
}
