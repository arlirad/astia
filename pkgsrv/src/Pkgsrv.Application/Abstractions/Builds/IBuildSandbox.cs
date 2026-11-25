using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Abstractions.Builds;

public enum BuildExecutionStatus
{
    NotExecuted = 0,
    Completed = 1,
    Failed = 2
}

public sealed record BuildSandboxRequest(
    PackageKey Package,
    PackageSource Source,
    BuildPolicy Policy);

public sealed record BuildSandboxResult(BuildExecutionStatus Status, string Detail)
{
    public static BuildSandboxResult NotExecuted(string detail) =>
        new(BuildExecutionStatus.NotExecuted, detail);
}

public interface IBuildSandbox
{
    Task<BuildSandboxResult> ExecuteAsync(
        BuildSandboxRequest request,
        CancellationToken cancellationToken);
}
