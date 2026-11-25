using Pkgsrv.Application.Features.Packages.RequestBuild;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Validation;

public static class RequestBuildValidator
{
    public static IReadOnlyList<ValidationFailure> Validate(RequestPackageBuildCommand command)
    {
        ArgumentNullException.ThrowIfNull(command);

        var failures = new List<ValidationFailure>();
        DomainValueValidator.Collect(
            () => PackageKey.Create(command.Name, command.Version),
            nameof(RequestPackageBuildCommand.Name),
            failures);

        return failures;
    }
}
