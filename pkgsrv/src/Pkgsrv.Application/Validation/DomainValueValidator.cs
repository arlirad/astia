using Pkgsrv.Domain.Errors;

namespace Pkgsrv.Application.Validation;

public static class DomainValueValidator
{
    public static void Collect<T>(
        Func<T> factory,
        string field,
        List<ValidationFailure> failures)
    {
        ArgumentNullException.ThrowIfNull(factory);
        ArgumentNullException.ThrowIfNull(failures);

        try
        {
            factory();
        }
        catch (DomainException exception)
        {
            failures.Add(new ValidationFailure(field, exception.Code, exception.Message));
        }
    }
}
