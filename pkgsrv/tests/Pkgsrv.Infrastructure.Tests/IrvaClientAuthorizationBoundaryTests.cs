using Pkgsrv.Application.Authorization;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Infrastructure.Security;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class IrvaClientAuthorizationBoundaryTests
{
    private static readonly Guid AccountId = Guid.Parse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
    private static readonly Guid OtherAccountId = Guid.Parse("11111111-2222-3333-4444-555555555555");

    [Fact]
    public async Task Allow_UsesVerifiedIrvaAccountAndPermission()
    {
        var adapter = new FakeAdapter
        {
            Account = new IrvaVerifiedAccount(AccountId, "alice"),
            Permission = true
        };
        var boundary = CreateBoundary(adapter, TransportIdentity.ForRequest("access-token"));

        var response = await boundary.CheckAsync(
            AuthorizationContext.Create("spoofed"),
            PackagePermissions.Read,
            null,
            CancellationToken.None);

        Assert.True(response.IsAvailable);
        Assert.True(response.IsAllowed);
        Assert.Equal(AccountId, response.VerifiedContext!.AccountId);
        Assert.Equal("alice", response.VerifiedContext.ActorId);
        Assert.Equal(AccountId, adapter.LastAccountId);
        Assert.Equal(PackagePermissions.Read, adapter.LastPermission);
        Assert.Equal(1, adapter.VerifyCalls);
    }

    [Fact]
    public async Task Deny_WhenVerifiedAccountLacksPermission()
    {
        var adapter = new FakeAdapter
        {
            Account = new IrvaVerifiedAccount(AccountId, "alice"),
            Permission = false
        };
        var boundary = CreateBoundary(adapter, TransportIdentity.ForRequest("access-token"));

        var response = await boundary.CheckAsync(
            TestPackages.Actor,
            PackagePermissions.Publish,
            null,
            CancellationToken.None);

        Assert.True(response.IsAvailable);
        Assert.False(response.IsAllowed);
        Assert.Equal(AuthorizationFailure.Forbidden, response.Failure);
        Assert.Equal(AccountId, response.VerifiedContext!.AccountId);
        Assert.Contains("lacks permission", response.Reason, StringComparison.Ordinal);
    }

    [Fact]
    public async Task InvalidToken_ReturnsAuthenticationRequired()
    {
        var adapter = new FakeAdapter
        {
            VerifyException = new IrvaClientAuthenticationException(
                "The bearer access token was rejected by Irva.")
        };
        var boundary = CreateBoundary(adapter, TransportIdentity.ForRequest("bad-token"));

        var response = await boundary.CheckAsync(
            TestPackages.Actor,
            PackagePermissions.Read,
            null,
            CancellationToken.None);

        Assert.Equal(AuthorizationFailure.AuthenticationRequired, response.Failure);
        Assert.False(response.IsAllowed);
        Assert.Equal(0, adapter.PermissionCalls);
    }

    [Fact]
    public async Task UnavailableEndpoint_ReturnsUnavailable()
    {
        var adapter = new FakeAdapter
        {
            VerifyException = new IrvaClientUnavailableException(
                "The Irva authorization endpoint is unreachable.")
        };
        var boundary = CreateBoundary(adapter, TransportIdentity.ForRequest("access-token"));

        var response = await boundary.CheckAsync(
            TestPackages.Actor,
            PackagePermissions.Read,
            null,
            CancellationToken.None);

        Assert.False(response.IsAvailable);
        Assert.False(response.IsAllowed);
        Assert.Equal(AuthorizationFailure.Unavailable, response.Failure);
    }

    [Fact]
    public async Task MissingBearer_DeniesWithoutCallingIrva()
    {
        var adapter = new FakeAdapter();
        var boundary = CreateBoundary(adapter, TransportIdentity.ForRequest(null));

        var response = await boundary.CheckAsync(
            AuthorizationContext.Create("spoofed", ["publish"], OtherAccountId),
            PackagePermissions.Read,
            null,
            CancellationToken.None);

        Assert.Equal(AuthorizationFailure.AuthenticationRequired, response.Failure);
        Assert.Equal(0, adapter.VerifyCalls);
        Assert.Null(response.VerifiedContext);
    }

    [Fact]
    public async Task AccountHeaderSpoof_DoesNotOverrideVerifiedIrvaAccount()
    {
        var adapter = new FakeAdapter
        {
            Account = new IrvaVerifiedAccount(AccountId, "alice"),
            Permission = true
        };
        var boundary = CreateBoundary(adapter, TransportIdentity.ForRequest("access-token"));
        var spoofed = AuthorizationContext.Create(
            "mallory",
            ["publisher"],
            OtherAccountId);

        var response = await boundary.CheckAsync(
            spoofed,
            PackagePermissions.Publish,
            null,
            CancellationToken.None);

        Assert.True(response.IsAllowed);
        Assert.Equal(AccountId, response.VerifiedContext!.AccountId);
        Assert.Equal("alice", response.VerifiedContext.ActorId);
        Assert.Equal(AccountId, adapter.LastAccountId);
    }

    [Fact]
    public async Task VerifiedTransportIdentity_SkipsRepeatedVerification()
    {
        var adapter = new FakeAdapter
        {
            Account = new IrvaVerifiedAccount(AccountId, "alice"),
            Permission = true
        };
        var transport = new FakeTransportIdentity(TransportIdentity.ForRequest("access-token"));
        transport.SetVerifiedIdentity(
            AuthorizationContext.Create("alice", accountId: AccountId));
        var boundary = new IrvaClientAuthorizationBoundary(adapter, transport);

        var response = await boundary.CheckAsync(
            TestPackages.Actor,
            PackagePermissions.Build,
            null,
            CancellationToken.None);

        Assert.True(response.IsAllowed);
        Assert.Equal(0, adapter.VerifyCalls);
        Assert.Equal(AccountId, adapter.LastAccountId);
    }

    [Theory]
    [InlineData(PackageAction.Read, PackagePermissions.Read)]
    [InlineData(PackageAction.Build, PackagePermissions.Build)]
    [InlineData(PackageAction.Publish, PackagePermissions.Publish)]
    public async Task Adapter_MapsActionsToIrvaPermissionNames(
        PackageAction action,
        string expectedPermission)
    {
        var boundary = new RecordingBoundary();
        var adapter = new IrvaClientAuthorizationAdapter(boundary);

        await adapter.AuthorizeAsync(
            TestPackages.Actor,
            action,
            null,
            CancellationToken.None);

        Assert.Equal(expectedPermission, boundary.LastPermission);
    }

    private static IrvaClientAuthorizationBoundary CreateBoundary(
        IIrvaClientAdapter adapter,
        TransportIdentity identity) =>
        new(adapter, new FakeTransportIdentity(identity));

    private sealed class RecordingBoundary : IIrvaClientAuthorizationBoundary
    {
        public string? LastPermission { get; private set; }

        public Task<IrvaClientAuthorizationResponse> CheckAsync(
            AuthorizationContext context,
            string permission,
            Pkgsrv.Domain.Packages.PackageKey? package,
            CancellationToken cancellationToken)
        {
            LastPermission = permission;
            return Task.FromResult(IrvaClientAuthorizationResponse.Denied("recorded"));
        }
    }

    private sealed class FakeAdapter : IIrvaClientAdapter
    {
        public IrvaVerifiedAccount Account { get; set; } =
            new(AccountId, "alice");

        public bool Permission { get; set; }

        public Exception? VerifyException { get; set; }

        public int VerifyCalls { get; private set; }

        public int PermissionCalls { get; private set; }

        public Guid? LastAccountId { get; private set; }

        public string? LastPermission { get; private set; }

        public Task<IrvaVerifiedAccount> VerifyAsync(
            string accessToken,
            CancellationToken cancellationToken)
        {
            VerifyCalls++;

            if (VerifyException is not null)
            {
                throw VerifyException;
            }

            return Task.FromResult(Account);
        }

        public bool HasPermission(Guid accountId, string permission)
        {
            PermissionCalls++;
            LastAccountId = accountId;
            LastPermission = permission;
            return Permission;
        }
    }

    private sealed class FakeTransportIdentity : ITransportIdentityAccessor
    {
        private TransportIdentity _identity;

        public FakeTransportIdentity(TransportIdentity identity)
        {
            _identity = identity;
        }

        public TransportIdentity GetCurrent() => _identity;

        public void SetVerifiedIdentity(AuthorizationContext actor)
        {
            _identity = _identity.WithVerifiedActor(actor);
        }
    }
}
