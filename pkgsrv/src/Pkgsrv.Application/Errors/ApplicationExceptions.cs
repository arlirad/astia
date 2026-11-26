namespace Pkgsrv.Application.Errors;

public abstract class PkgsrvApplicationException : Exception
{
    protected PkgsrvApplicationException(string code, string message)
        : base(message)
    {
        Code = code;
    }

    public string Code { get; }
}

public sealed class AuthorizationDeniedException : PkgsrvApplicationException
{
    public AuthorizationDeniedException(string action, string reason)
        : base("authorization.denied", $"Authorization denied for {action}: {reason}")
    {
        Action = action;
        Reason = reason;
    }

    public string Action { get; }

    public string Reason { get; }
}

public sealed class PackageNotFoundException : PkgsrvApplicationException
{
    public PackageNotFoundException(string package)
        : base("package.not-found", $"Package {package} was not found.")
    {
        Package = package;
    }

    public string Package { get; }
}

public sealed class PackageConflictException : PkgsrvApplicationException
{
    public PackageConflictException(string package, string? detail = null)
        : base(
            "package.conflict",
            detail ?? $"Package {package} already exists.")
    {
        Package = package;
    }

    public string Package { get; }
}

public sealed class SourceNotAllowedException : PkgsrvApplicationException
{
    public SourceNotAllowedException(string host)
        : base("package.source.not-allowed", $"Source host {host} is not on the package source allowlist.")
    {
        Host = host;
    }

    public string Host { get; }
}

public sealed class PackagePublishException : PkgsrvApplicationException
{
    public PackagePublishException(string package, string reason)
        : base("package.publish-refused", $"Package {package} could not be published: {reason}")
    {
        Package = package;
    }

    public string Package { get; }
}

public sealed class PackageArtifactNotFoundException : PkgsrvApplicationException
{
    public PackageArtifactNotFoundException(string package, string contentHash)
        : base(
            "package.artifact-not-found",
            $"Artifact {contentHash} for package {package} was not found.")
    {
        Package = package;
        ContentHash = contentHash;
    }

    public string Package { get; }

    public string ContentHash { get; }
}

public sealed class PackageSignatureInvalidException : PkgsrvApplicationException
{
    public PackageSignatureInvalidException(string package)
        : base(
            "package.signature-invalid",
            $"Package {package} does not have a valid publishable signature.")
    {
        Package = package;
    }

    public string Package { get; }
}

public sealed class PackageNotPublishedException : PkgsrvApplicationException
{
    public PackageNotPublishedException(string package)
        : base(
            "package.not-published",
            $"Package {package} has not been durably published.")
    {
        Package = package;
    }

    public string Package { get; }
}
