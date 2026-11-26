using Microsoft.AspNetCore.Http;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Presentation.Security;

namespace Pkgsrv.Presentation.Tests;

public sealed class HttpTransportIdentityAccessorTests
{
    private static readonly Guid AccountId =
        Guid.Parse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");

    [Fact]
    public void MissingBearer_IsAnonymousAndIgnoresSpoofedHeaders()
    {
        var httpContext = new DefaultHttpContext();
        httpContext.Request.Headers["X-Pkgsrv-Actor"] = "mallory";
        httpContext.Request.Headers["X-Pkgsrv-Roles"] = "publisher,builder";
        httpContext.Request.Headers["X-Pkgsrv-Account"] =
            Guid.NewGuid().ToString();

        var identity = CreateAccessor(httpContext).GetCurrent();

        Assert.Null(identity.AccessToken);
        Assert.False(identity.IsVerified);
        Assert.True(identity.Actor.IsAnonymous);
    }

    [Fact]
    public void ReadsBearerToken()
    {
        var httpContext = new DefaultHttpContext();
        httpContext.Request.Headers.Authorization = "Bearer access-token";

        var identity = CreateAccessor(httpContext).GetCurrent();

        Assert.Equal("access-token", identity.AccessToken);
        Assert.False(identity.IsVerified);
    }

    [Theory]
    [InlineData("Basic access-token")]
    [InlineData("Bearer")]
    [InlineData("Bearer ")]
    public void RejectsNonBearerAuthorization(string value)
    {
        var httpContext = new DefaultHttpContext();
        httpContext.Request.Headers.Authorization = value;

        var identity = CreateAccessor(httpContext).GetCurrent();

        Assert.Null(identity.AccessToken);
    }

    [Fact]
    public void VerifiedIdentityPropagatesAndOverridesSpoofedHeaders()
    {
        var httpContext = new DefaultHttpContext();
        httpContext.Request.Headers.Authorization = "Bearer access-token";
        httpContext.Request.Headers["X-Pkgsrv-Actor"] = "mallory";
        var accessor = CreateAccessor(httpContext);
        var verified = AuthorizationContext.Create("alice", accountId: AccountId);

        accessor.SetVerifiedIdentity(verified);
        var identity = accessor.GetCurrent();

        Assert.True(identity.IsVerified);
        Assert.Equal("alice", identity.Actor.ActorId);
        Assert.Equal(AccountId, identity.Actor.AccountId);
    }

    [Fact]
    public void SetVerifiedIdentityRequiresAccountId()
    {
        var accessor = CreateAccessor(new DefaultHttpContext());

        Assert.Throws<ArgumentException>(
            () => accessor.SetVerifiedIdentity(AuthorizationContext.Create("alice")));
    }

    [Fact]
    public void ToStringDoesNotExposeBearerToken()
    {
        var httpContext = new DefaultHttpContext();
        httpContext.Request.Headers.Authorization = "Bearer super-secret-token";

        var identity = CreateAccessor(httpContext).GetCurrent();

        Assert.DoesNotContain("super-secret-token", identity.ToString(), StringComparison.Ordinal);
    }

    private static HttpTransportIdentityAccessor CreateAccessor(HttpContext httpContext) =>
        new(new HttpContextAccessor { HttpContext = httpContext });
}
