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
    public PackageConflictException(string package)
        : base("package.conflict", $"Package {package} already exists.")
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
