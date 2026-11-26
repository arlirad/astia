using System.Reflection;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Validation;
using Pkgsrv.Domain.Errors;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Presentation.Api;

public static class ApiErrorMapper
{
    public static bool IsMapped(Exception exception) =>
        Unwrap(exception) is ValidationFailedException
            or AuthenticationRequiredException
            or AuthorizationDeniedException
            or AuthorizationUnavailableException
            or PackageNotFoundException
            or PackageConflictException
            or PackageArtifactNotFoundException
            or PackagePublishException
            or PackageNotPublishedException
            or PackageSignatureInvalidException
            or SourceNotAllowedException
            or DomainException;

    public static IResult Map(Exception exception)
    {
        var unwrapped = Unwrap(exception);

        return unwrapped switch
        {
            ValidationFailedException validation => ValidationProblem(validation),
            AuthenticationRequiredException unauthenticated => Problem(
                401,
                "Authentication required",
                unauthenticated.Message,
                unauthenticated.Code),
            AuthorizationDeniedException denied => Problem(
                403,
                "Authorization denied",
                denied.Message,
                denied.Code),
            AuthorizationUnavailableException unavailable => Problem(
                503,
                "Authorization unavailable",
                unavailable.Message,
                unavailable.Code),
            PackageNotFoundException notFound => Problem(
                404,
                "Package not found",
                notFound.Message,
                notFound.Code),
            PackageConflictException conflict => Problem(
                409,
                "Package conflict",
                conflict.Message,
                conflict.Code),
            PackageArtifactNotFoundException artifact => Problem(
                404,
                "Package artifact not found",
                artifact.Message,
                artifact.Code),
            PackagePublishException publish => Problem(
                409,
                "Package not publishable",
                publish.Message,
                publish.Code),
            PackageNotPublishedException notPublished => Problem(
                409,
                "Package not published",
                notPublished.Message,
                notPublished.Code),
            PackageSignatureInvalidException signature => Problem(
                409,
                "Package signature invalid",
                signature.Message,
                signature.Code),
            SourceNotAllowedException source => Problem(
                422,
                "Source not allowed",
                source.Message,
                source.Code),
            DomainException domain when domain.Code == DomainErrorCodes.BuildAlreadyRequested => Problem(
                409,
                "Build already requested",
                domain.Message,
                domain.Code),
            DomainException domain => Problem(
                400,
                "Domain rule violated",
                domain.Message,
                domain.Code),
            _ => Problem(500, "Unexpected error", "An unexpected error occurred.", "server.error")
        };
    }

    private static Exception Unwrap(Exception exception)
    {
        var current = exception;

        while (current is TargetInvocationException invocation)
        {
            if (invocation.InnerException is null)
            {
                break;
            }

            current = invocation.InnerException;
        }

        return current;
    }

    private static IResult ValidationProblem(ValidationFailedException exception)
    {
        var errors = exception.Failures
            .GroupBy(failure => failure.Field)
            .ToDictionary(
                group => group.Key,
                group => group.Select(failure => failure.Message).ToArray());

        return Results.Problem(
            statusCode: 400,
            title: "Validation failed",
            detail: exception.Message,
            extensions: new Dictionary<string, object?>
            {
                ["code"] = exception.Code,
                ["errors"] = errors
            });
    }

    private static IResult Problem(int statusCode, string title, string detail, string code) =>
        Results.Problem(
            statusCode: statusCode,
            title: title,
            detail: detail,
            extensions: new Dictionary<string, object?>
            {
                ["code"] = code
            });
}
