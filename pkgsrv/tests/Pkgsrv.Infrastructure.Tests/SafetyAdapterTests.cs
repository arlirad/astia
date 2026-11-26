using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Application.Interfaces.Builds;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Artifacts;
using Pkgsrv.Infrastructure.Builds;
using Pkgsrv.Infrastructure.Events;
using Pkgsrv.Infrastructure.Security;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class SafetyAdapterTests
{
    [Fact]
    public async Task BuildSandbox_ReturnsNotExecuted()
    {
        var sandbox = new NonExecutingBuildSandbox();
        var package = TestPackages.CreatePackage();

        var result = await sandbox.ExecuteAsync(
            new BuildSandboxRequest(package.Key, package.Manifest.Source, package.BuildPolicy),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Contains("No process was started", result.Detail, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Publisher_RefusesPendingSignature()
    {
        var publisher = new RefusingUnsignedPackagePublisher();
        var package = TestPackages.CreatePackage();
        var artifact = new ArtifactMetadata(
            package.Key,
            package.Manifest.ContentHash.Value,
            "artifact-signature",
            TestPackages.Timestamp);

        var result = await publisher.PublishAsync(
            package.Manifest,
            artifact,
            CancellationToken.None);

        Assert.False(result.Published);
        Assert.Contains("not signed", result.Detail, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Publisher_RefusesUnsignedArtifactAndAcceptsSignedArtifact()
    {
        var publisher = new RefusingUnsignedPackagePublisher();
        var package = TestPackages.CreatePackage(signed: true);
        var unsignedArtifact = new ArtifactMetadata(
            package.Key,
            package.Manifest.ContentHash.Value,
            null,
            TestPackages.Timestamp);
        var signedArtifact = unsignedArtifact with { Signature = "artifact-signature" };

        var refused = await publisher.PublishAsync(
            package.Manifest,
            unsignedArtifact,
            CancellationToken.None);
        var published = await publisher.PublishAsync(
            package.Manifest,
            signedArtifact,
            CancellationToken.None);

        Assert.False(refused.Published);
        Assert.True(published.Published);
    }

    [Fact]
    public async Task Publisher_RefusesArtifactForDifferentPackageWithSameHash()
    {
        var publisher = new RefusingUnsignedPackagePublisher();
        var package = TestPackages.CreatePackage(signed: true);
        var artifact = new ArtifactMetadata(
            PackageKey.Create("other-package", "1.2.3"),
            package.Manifest.ContentHash.Value,
            "artifact-signature",
            TestPackages.Timestamp);

        var result = await publisher.PublishAsync(
            package.Manifest,
            artifact,
            CancellationToken.None);

        Assert.False(result.Published);
        Assert.Contains("different package", result.Detail, StringComparison.Ordinal);
    }

    [Fact]
    public async Task PendingSigner_LeavesPackageUnsigned()
    {
        var signer = new PendingPackageSigner();

        var result = await signer.SignAsync(
            ContentHash.Create(TestPackages.ContentHashValue),
            CancellationToken.None);

        Assert.False(result.Signed);
        Assert.Equal(PackageSignatureStatus.Pending, result.Signature.Status);
    }

    [Fact]
    public async Task PendingIrvaAdapter_MissingBearerIsAuthenticationRequired()
    {
        var boundary = new PendingIrvaClientAuthorizationBoundary(
            new StubTransportIdentity(TransportIdentity.ForRequest(null)));
        var adapter = new IrvaClientAuthorizationAdapter(boundary);

        var result = await adapter.AuthorizeAsync(
            TestPackages.Actor,
            PackageAction.Read,
            null,
            CancellationToken.None);

        Assert.False(result.IsAllowed);
        Assert.Equal(AuthorizationFailure.AuthenticationRequired, result.Failure);
        Assert.Contains("bearer", result.Reason, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task PendingIrvaAdapter_BearerTokenIsUnavailableAndDenied()
    {
        var boundary = new PendingIrvaClientAuthorizationBoundary(
            new StubTransportIdentity(TransportIdentity.ForRequest("access-token")));
        var adapter = new IrvaClientAuthorizationAdapter(boundary);

        var result = await adapter.AuthorizeAsync(
            TestPackages.Actor,
            PackageAction.Read,
            null,
            CancellationToken.None);

        Assert.False(result.IsAllowed);
        Assert.Equal(AuthorizationFailure.Unavailable, result.Failure);
        Assert.Contains("disabled", result.Reason, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task IrvaAdapter_ForwardsVerifiedBoundaryDecisions()
    {
        var verified = AuthorizationContext.Create(
            "alice",
            accountId: Guid.Parse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"));
        var boundary = new StubBoundary(
            IrvaClientAuthorizationResponse.Allowed(verified, "verified"));
        var adapter = new IrvaClientAuthorizationAdapter(boundary);

        var result = await adapter.AuthorizeAsync(
            TestPackages.Actor,
            PackageAction.Build,
            PackageKey.Create("widget", "1.2.3"),
            CancellationToken.None);

        Assert.True(result.IsAllowed);
        Assert.Equal("verified", result.Reason);
        Assert.Equal(verified, result.VerifiedContext);
        Assert.Equal("build", boundary.LastPermission);
    }

    [Fact]
    public async Task IrvaAdapter_ForwardsDeniedBoundaryDecisions()
    {
        var adapter = new IrvaClientAuthorizationAdapter(
            new StubBoundary(IrvaClientAuthorizationResponse.Denied("not permitted")));

        var result = await adapter.AuthorizeAsync(
            TestPackages.Actor,
            PackageAction.Publish,
            null,
            CancellationToken.None);

        Assert.False(result.IsAllowed);
        Assert.Equal(AuthorizationFailure.Forbidden, result.Failure);
        Assert.Equal("not permitted", result.Reason);
    }

    [Fact]
    public async Task ArtifactMetadataStore_RoundTrips()
    {
        var store = new InMemoryArtifactMetadataStore();
        var package = TestPackages.CreatePackage();
        var metadata = new ArtifactMetadata(
            package.Key,
            package.Manifest.ContentHash.Value,
            null,
            TestPackages.Timestamp);

        await store.SaveAsync(metadata, CancellationToken.None);

        Assert.Equal(metadata, await store.FindAsync(package.Key, CancellationToken.None));
    }

    [Fact]
    public async Task EventPublisher_RecordsPublishedEvents()
    {
        var publisher = new InMemoryDomainEventPublisher();
        var package = TestPackages.CreatePackage();

        await publisher.PublishAsync(package.PeekDomainEvents(), CancellationToken.None);

        Assert.Single(publisher.Events);
    }

    private sealed class StubBoundary : IIrvaClientAuthorizationBoundary
    {
        private readonly IrvaClientAuthorizationResponse _response;

        public StubBoundary(IrvaClientAuthorizationResponse response)
        {
            _response = response;
        }

        public string? LastPermission { get; private set; }

        public Task<IrvaClientAuthorizationResponse> CheckAsync(
            AuthorizationContext context,
            string permission,
            PackageKey? package,
            CancellationToken cancellationToken)
        {
            LastPermission = permission;
            return Task.FromResult(_response);
        }
    }

    private sealed class StubTransportIdentity : ITransportIdentityAccessor
    {
        private TransportIdentity _identity;

        public StubTransportIdentity(TransportIdentity identity)
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
