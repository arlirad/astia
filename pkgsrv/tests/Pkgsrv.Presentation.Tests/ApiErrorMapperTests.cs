using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Http.HttpResults;
using Pkgsrv.Application.Errors;
using Pkgsrv.Presentation.Api;

namespace Pkgsrv.Presentation.Tests;

public sealed class ApiErrorMapperTests
{
    [Fact]
    public void AuthenticationRequired_MapsToUnauthorizedProblem()
    {
        var result = Assert.IsType<ProblemHttpResult>(
            ApiErrorMapper.Map(new AuthenticationRequiredException(
                "Read",
                "A bearer access token is required.")));

        Assert.Equal(401, result.ProblemDetails.Status);
        Assert.Equal("authorization.unauthenticated", result.ProblemDetails.Extensions["code"]);
    }

    [Fact]
    public void PermissionDenial_MapsToForbiddenProblem()
    {
        var result = Assert.IsType<ProblemHttpResult>(
            ApiErrorMapper.Map(new AuthorizationDeniedException(
                "Read",
                "Irva verified account lacks permission 'read'.")));

        Assert.Equal(403, result.ProblemDetails.Status);
        Assert.Equal("authorization.denied", result.ProblemDetails.Extensions["code"]);
    }

    [Fact]
    public void AuthorizationUnavailable_MapsToServiceUnavailableProblem()
    {
        var result = Assert.IsType<ProblemHttpResult>(
            ApiErrorMapper.Map(new AuthorizationUnavailableException(
                "Read",
                "Irva authorization is unavailable.")));

        Assert.Equal(503, result.ProblemDetails.Status);
        Assert.Equal("authorization.unavailable", result.ProblemDetails.Extensions["code"]);
    }
}
