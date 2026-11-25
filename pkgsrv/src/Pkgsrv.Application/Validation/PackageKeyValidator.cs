using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Validation;

public static class PackageKeyValidator
{
    public static IReadOnlyList<ValidationFailure> Validate(string? name, string? version)
    {
        var failures = new List<ValidationFailure>();

        DomainValueValidator.Collect(
            () => PackageName.Create(name),
            nameof(PackageKey.Name),
            failures);
        DomainValueValidator.Collect(
            () => PackageVersion.Create(version),
            nameof(PackageKey.Version),
            failures);

        return failures;
    }
}
