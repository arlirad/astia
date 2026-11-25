namespace Pkgsrv.Application.Validation;

public sealed record ValidationFailure(string Field, string Code, string Message);

public sealed class ValidationFailedException : Exception
{
    public ValidationFailedException(IReadOnlyList<ValidationFailure> failures)
        : base("The request failed validation.")
    {
        ArgumentNullException.ThrowIfNull(failures);
        Failures = failures;
    }

    public string Code => "request.validation.failed";

    public IReadOnlyList<ValidationFailure> Failures { get; }
}
