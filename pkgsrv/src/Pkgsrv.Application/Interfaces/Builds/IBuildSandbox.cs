using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Interfaces.Builds;

public enum BuildExecutionStatus
{
    NotExecuted = 0,
    Completed = 1,
    Failed = 2
}

public sealed record BuildSandboxRequest(
    PackageKey Package,
    PackageSource Source,
    BuildPolicy Policy,
    Toolchain? Toolchain = null,
    ContentHash? ExpectedArtifactHash = null);

public sealed record BuildSandboxResult(
    BuildExecutionStatus Status,
    string Detail,
    ContentHash? ArtifactHash = null)
{
    public static BuildSandboxResult NotExecuted(string detail) =>
        new(BuildExecutionStatus.NotExecuted, detail);

    public static BuildSandboxResult Failed(string detail) =>
        new(BuildExecutionStatus.Failed, detail);
}

public interface IBuildSandbox
{
    Task<BuildSandboxResult> ExecuteAsync(
        BuildSandboxRequest request,
        CancellationToken cancellationToken);
}
