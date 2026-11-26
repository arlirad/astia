using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Builds;

public enum BuildProfileNetworkMode
{
    Offline = 0,
    EgressProxy = 1
}

public sealed record BuildProfile(
    string ToolchainName,
    string ToolchainVersion,
    string Executable,
    IReadOnlyList<string> Arguments,
    string OutputFile,
    BuildProfileNetworkMode NetworkMode = BuildProfileNetworkMode.Offline)
{
    public bool Matches(Toolchain toolchain) =>
        string.Equals(ToolchainName, toolchain.Name, StringComparison.OrdinalIgnoreCase)
        && string.Equals(ToolchainVersion, toolchain.Version, StringComparison.OrdinalIgnoreCase);
}

public sealed record BuildSandboxOptions(
    string BubblewrapPath,
    string WorkspaceRoot,
    IReadOnlyList<BuildProfile> Profiles,
    IReadOnlyList<string> ReadOnlyPaths,
    long MaxArtifactBytes = 256L * 1024 * 1024,
    long WorkspaceQuotaBytes = 0,
    long WorkspaceQuotaInodes = 0,
    bool AcceptNamespaceOnlyWithoutResourceLimits = false,
    bool RequireDurableArtifactStore = false,
    string? ArtifactRoot = null,
    long ArtifactQuotaBytes = 0,
    long ArtifactQuotaInodes = 0,
    long RequiredArtifactBytes = 0,
    Func<string, long, long, bool>? WorkspaceQuotaValidator = null);
