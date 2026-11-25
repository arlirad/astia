using Pkgsrv.Application.Abstractions.Builds;

namespace Pkgsrv.Infrastructure.Builds;

public sealed class NonExecutingBuildSandbox : IBuildSandbox
{
    public Task<BuildSandboxResult> ExecuteAsync(
        BuildSandboxRequest request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);

        return Task.FromResult(BuildSandboxResult.NotExecuted(
            "Build execution is disabled by the Pkgsrv safe default. No process was started."));
    }
}
