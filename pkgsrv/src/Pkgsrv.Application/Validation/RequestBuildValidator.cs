using Pkgsrv.Application.Features.Packages.RequestBuild;

namespace Pkgsrv.Application.Validation;

public static class RequestBuildValidator
{
    public static IReadOnlyList<ValidationFailure> Validate(RequestPackageBuildCommand command)
    {
        ArgumentNullException.ThrowIfNull(command);

        return PackageKeyValidator.Validate(command.Name, command.Version);
    }
}
