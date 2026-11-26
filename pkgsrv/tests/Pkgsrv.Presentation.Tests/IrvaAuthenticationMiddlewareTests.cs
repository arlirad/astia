using Microsoft.AspNetCore.Http;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Presentation.Security;

namespace Pkgsrv.Presentation.Tests;

public sealed class IrvaAuthenticationMiddlewareTests
{
    private static readonly Guid AccountId =
        Guid.Parse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");

    [Fact]
    public async Task VerifiedTokenPopulatesTransportIdentity()
    {
        var httpContext = CreateContext("Bearer access-token");
        var accessor = CreateAccessor(httpContext);
        var verifier = new StubVerifier(
            IdentityVerificationResult.Success(
                AuthorizationContext.Create("alice", accountId: AccountId)));
        var middleware = new IrvaAuthenticationMiddleware(_ => Task.CompletedTask);

        await middleware.InvokeAsync(httpContext, accessor, verifier);

        var identity = accessor.GetCurrent();
        Assert.True(identity.IsVerified);
        Assert.Equal(AccountId, identity.Actor.AccountId);
        Assert.Equal("alice", identity.Actor.ActorId);
        Assert.Equal(1, verifier.Calls);
    }

    [Fact]
    public async Task InvalidTokenLeavesAnonymousIdentity()
    {
        var httpContext = CreateContext("Bearer bad-token");
        var accessor = CreateAccessor(httpContext);
        var verifier = new StubVerifier(
            IdentityVerificationResult.Invalid("The bearer access token was rejected by Irva."));
        var middleware = new IrvaAuthenticationMiddleware(_ => Task.CompletedTask);

        await middleware.InvokeAsync(httpContext, accessor, verifier);

        Assert.True(accessor.GetCurrent().Actor.IsAnonymous);
        Assert.False(accessor.GetCurrent().IsVerified);
    }

    [Fact]
    public async Task MissingBearerDoesNotCallVerifier()
    {
        var httpContext = CreateContext(null);
        var accessor = CreateAccessor(httpContext);
        var verifier = new StubVerifier(
            IdentityVerificationResult.Success(
                AuthorizationContext.Create("alice", accountId: AccountId)));
        var middleware = new IrvaAuthenticationMiddleware(_ => Task.CompletedTask);

        await middleware.InvokeAsync(httpContext, accessor, verifier);

        Assert.Equal(0, verifier.Calls);
        Assert.True(accessor.GetCurrent().Actor.IsAnonymous);
    }

    [Fact]
    public async Task VerifierFailureLeavesAnonymousIdentity()
    {
        var httpContext = CreateContext("Bearer access-token");
        var accessor = CreateAccessor(httpContext);
        var verifier = new StubVerifier(throwOnVerify: true);
        var middleware = new IrvaAuthenticationMiddleware(_ => Task.CompletedTask);

        await middleware.InvokeAsync(httpContext, accessor, verifier);

        Assert.True(accessor.GetCurrent().Actor.IsAnonymous);
        Assert.False(accessor.GetCurrent().IsVerified);
    }

    private static DefaultHttpContext CreateContext(string? authorization)
    {
        var context = new DefaultHttpContext();

        if (authorization is not null)
        {
            context.Request.Headers.Authorization = authorization;
        }

        return context;
    }

    private static HttpTransportIdentityAccessor CreateAccessor(HttpContext context) =>
        new(new HttpContextAccessor { HttpContext = context });

    private sealed class StubVerifier : IIrvaIdentityVerifier
    {
        private readonly IdentityVerificationResult? _result;
        private readonly bool _throwOnVerify;

        public StubVerifier(
            IdentityVerificationResult? result = null,
            bool throwOnVerify = false)
        {
            _result = result;
            _throwOnVerify = throwOnVerify;
        }

        public int Calls { get; private set; }

        public Task<IdentityVerificationResult> VerifyAsync(
            string accessToken,
            CancellationToken cancellationToken)
        {
            Calls++;

            if (_throwOnVerify)
            {
                throw new InvalidOperationException("verification failed");
            }

            return Task.FromResult(_result!);
        }
    }
}
