using Pkgsrv.Application.Features.Packages.Register;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Validation;

public static class PackageRegistrationValidator
{
    public static IReadOnlyList<ValidationFailure> Validate(RegisterPackageCommand command)
    {
        ArgumentNullException.ThrowIfNull(command);

        var failures = new List<ValidationFailure>();

        DomainValueValidator.Collect(
            () => PackageName.Create(command.Name),
            nameof(RegisterPackageCommand.Name),
            failures);
        DomainValueValidator.Collect(
            () => PackageVersion.Create(command.Version),
            nameof(RegisterPackageCommand.Version),
            failures);
        DomainValueValidator.Collect(
            () => PackageSource.Create(command.SourceUrl, command.SourceCommit),
            nameof(RegisterPackageCommand.SourceUrl),
            failures);
        DomainValueValidator.Collect(
            () => Toolchain.Create(command.ToolchainName, command.ToolchainVersion),
            nameof(RegisterPackageCommand.ToolchainName),
            failures);
        DomainValueValidator.Collect(
            () => ContentHash.Create(command.ContentHash),
            nameof(RegisterPackageCommand.ContentHash),
            failures);
        DomainValueValidator.Collect(
            () => PackageProvenance.Create(command.Provenance),
            nameof(RegisterPackageCommand.Provenance),
            failures);

        if (string.IsNullOrWhiteSpace(command.Signature))
        {
            if (!string.IsNullOrWhiteSpace(command.SignatureAlgorithm))
            {
                failures.Add(new ValidationFailure(
                    nameof(RegisterPackageCommand.Signature),
                    "package.signature.invalid",
                    "A signature algorithm cannot be supplied without a signature value."));
            }
        }
        else
        {
            DomainValueValidator.Collect(
                () => PackageSignature.Signed(command.SignatureAlgorithm, command.Signature),
                nameof(RegisterPackageCommand.Signature),
                failures);
        }

        if (!Enum.TryParse<BuildIsolation>(command.Isolation, true, out var isolation)
            || !Enum.IsDefined(isolation))
        {
            failures.Add(new ValidationFailure(
                nameof(RegisterPackageCommand.Isolation),
                "package.build.policy.invalid",
                "Build isolation must be Namespace, Container, or VirtualMachine."));
        }
        else
        {
            DomainValueValidator.Collect(
                () => BuildPolicy.Create(
                    isolation,
                    command.AllowNetwork,
                    TimeSpan.FromSeconds(command.TimeoutSeconds)),
                nameof(RegisterPackageCommand.TimeoutSeconds),
                failures);
        }

        return failures;
    }
}
