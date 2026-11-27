using Microsoft.Extensions.Configuration;
using Pkgsrv.Infrastructure.Builds;

namespace Pkgsrv.Presentation.Configuration;

public sealed class PkgsrvBuildProfileOptions
{
    public string? ToolchainName { get; set; }

    public string? ToolchainVersion { get; set; }

    public string? Executable { get; set; }

    public string[]? Arguments { get; set; }

    public string? OutputFile { get; set; }

    public string? NetworkMode { get; set; }
}

public sealed record PkgsrvBuildOptions(
    bool Enabled,
    string BubblewrapPath,
    string WorkspaceRoot,
    IReadOnlyList<PkgsrvBuildProfileOptions> Profiles,
    IReadOnlyList<string> ReadOnlyPaths,
    long MaxArtifactBytes,
    long WorkspaceQuotaBytes,
    long WorkspaceQuotaInodes,
    bool AcceptNamespaceOnlyWithoutResourceLimits)
{
    public const string DefaultBubblewrapPath = "/usr/bin/bwrap";
    public const string DefaultWorkspaceRoot = "/run/pkgsrv/builds";
    public const long DefaultMaxArtifactBytes = 256L * 1024 * 1024;
    public const long MaximumArtifactBytes = 1024L * 1024 * 1024;
    public const long MaximumWorkspaceQuotaBytes = 64L * 1024 * 1024 * 1024;
    public const long MaximumWorkspaceQuotaInodes = 10_000_000;

    private static readonly string[] DefaultReadOnlyPaths = ["/usr", "/bin", "/lib", "/lib64"];

    public static PkgsrvBuildOptions FromConfiguration(IConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(configuration);

        var section = configuration.GetSection("Pkgsrv:Build");
        var enabledRaw = section["Enabled"];
        var enabled = false;

        if (enabledRaw is not null
            && (string.IsNullOrWhiteSpace(enabledRaw)
                || !bool.TryParse(enabledRaw, out enabled)))
        {
            throw new InvalidOperationException(
                "Pkgsrv:Build:Enabled must be true or false when supplied.");
        }

        var bubblewrapPath = ReadAbsolutePath(
            section["BubblewrapPath"],
            DefaultBubblewrapPath,
            "Pkgsrv:Build:BubblewrapPath");
        var workspaceRoot = ReadAbsolutePath(
            section["WorkspaceRoot"],
            DefaultWorkspaceRoot,
            "Pkgsrv:Build:WorkspaceRoot");
        var readOnlyPaths = section.GetSection("ReadOnlyPaths").Get<string[]>();
        readOnlyPaths = readOnlyPaths is { Length: > 0 }
            ? readOnlyPaths
            : DefaultReadOnlyPaths;

        if (readOnlyPaths.Any(path => string.IsNullOrWhiteSpace(path)
            || !Path.IsPathFullyQualified(path)))
        {
            throw new InvalidOperationException(
                "Pkgsrv:Build:ReadOnlyPaths entries must be absolute paths.");
        }

        var profiles = section.GetSection("Profiles").Get<PkgsrvBuildProfileOptions[]>()
            ?? Array.Empty<PkgsrvBuildProfileOptions>();
        var acceptNamespaceOnly = ReadBoolean(
            section["AcceptNamespaceOnlyWithoutResourceLimits"],
            false,
            "Pkgsrv:Build:AcceptNamespaceOnlyWithoutResourceLimits");
        var maxArtifactBytes = ReadMaxArtifactBytes(section["MaxArtifactBytes"]);
        var workspaceQuotaBytes = ReadWorkspaceQuotaBytes(section["WorkspaceQuotaBytes"]);
        var workspaceQuotaInodes = ReadWorkspaceQuotaInodes(section["WorkspaceQuotaInodes"]);

        if (enabled && !acceptNamespaceOnly)
        {
            throw new InvalidOperationException(
                "Pkgsrv:Build:AcceptNamespaceOnlyWithoutResourceLimits must be true to enable bubblewrap Namespace-only builds without cgroup CPU/memory limits.");
        }

        if (enabled && workspaceQuotaBytes <= 0)
        {
            throw new InvalidOperationException(
                "Pkgsrv:Build:WorkspaceQuotaBytes must be configured to a bounded size before builds can be enabled; the root must be a dedicated tmpfs mount no larger than that quota.");
        }

        if (enabled && workspaceQuotaInodes <= 0)
        {
            throw new InvalidOperationException(
                "Pkgsrv:Build:WorkspaceQuotaInodes must be configured to a bounded inode count before builds can be enabled; the root must be a dedicated tmpfs mount no larger than that quota.");
        }

        if (enabled)
        {
            foreach (var profile in profiles)
            {
                ValidateProfile(profile);
            }
        }

        return new PkgsrvBuildOptions(
            enabled,
            bubblewrapPath,
            workspaceRoot,
            profiles,
            readOnlyPaths,
            maxArtifactBytes,
            workspaceQuotaBytes,
            workspaceQuotaInodes,
            acceptNamespaceOnly);
    }

    public BuildSandboxOptions ToSandboxOptions(
        string? artifactRoot = null,
        long artifactQuotaBytes = 0,
        long artifactQuotaInodes = 0,
        long requiredArtifactBytes = 0) => new(
        BubblewrapPath,
        WorkspaceRoot,
        Profiles.Select(profile => new BuildProfile(
            profile.ToolchainName!,
            profile.ToolchainVersion!,
            profile.Executable!,
            profile.Arguments ?? Array.Empty<string>(),
            profile.OutputFile!,
            ParseNetworkMode(profile.NetworkMode))).ToArray(),
        ReadOnlyPaths,
        MaxArtifactBytes,
        WorkspaceQuotaBytes,
        WorkspaceQuotaInodes,
        AcceptNamespaceOnlyWithoutResourceLimits,
        RequireDurableArtifactStore: true,
        ArtifactRoot: artifactRoot,
        ArtifactQuotaBytes: artifactQuotaBytes,
        ArtifactQuotaInodes: artifactQuotaInodes,
        RequiredArtifactBytes: requiredArtifactBytes);

    private static string ReadAbsolutePath(string? value, string defaultValue, string key)
    {
        var path = value?.Trim();

        if (string.IsNullOrWhiteSpace(path))
        {
            path = defaultValue;
        }

        if (!Path.IsPathFullyQualified(path))
        {
            throw new InvalidOperationException($"{key} must be an absolute path.");
        }

        return path;
    }

    private static bool ReadBoolean(string? value, bool defaultValue, string key)
    {
        if (value is null)
        {
            return defaultValue;
        }

        if (string.IsNullOrWhiteSpace(value) || !bool.TryParse(value, out var parsed))
        {
            throw new InvalidOperationException($"{key} must be true or false when supplied.");
        }

        return parsed;
    }

    private static long ReadMaxArtifactBytes(string? value)
    {
        if (value is null)
        {
            return DefaultMaxArtifactBytes;
        }

        if (string.IsNullOrWhiteSpace(value)
            || !long.TryParse(value, out var parsed)
            || parsed <= 0
            || parsed > MaximumArtifactBytes)
        {
            throw new InvalidOperationException(
                $"Pkgsrv:Build:MaxArtifactBytes must be between 1 and {MaximumArtifactBytes}.");
        }

        return parsed;
    }

    private static long ReadWorkspaceQuotaBytes(string? value)
    {
        if (value is null)
        {
            return 0;
        }

        if (string.IsNullOrWhiteSpace(value)
            || !long.TryParse(value, out var parsed)
            || parsed <= 0
            || parsed > MaximumWorkspaceQuotaBytes)
        {
            throw new InvalidOperationException(
                $"Pkgsrv:Build:WorkspaceQuotaBytes must be between 1 and {MaximumWorkspaceQuotaBytes}.");
        }

        return parsed;
    }

    private static long ReadWorkspaceQuotaInodes(string? value)
    {
        if (value is null)
        {
            return 0;
        }

        if (string.IsNullOrWhiteSpace(value)
            || !long.TryParse(value, out var parsed)
            || parsed <= 0
            || parsed > MaximumWorkspaceQuotaInodes)
        {
            throw new InvalidOperationException(
                $"Pkgsrv:Build:WorkspaceQuotaInodes must be between 1 and {MaximumWorkspaceQuotaInodes}.");
        }

        return parsed;
    }

    private static void ValidateProfile(PkgsrvBuildProfileOptions profile)
    {
        if (string.IsNullOrWhiteSpace(profile.ToolchainName)
            || string.IsNullOrWhiteSpace(profile.ToolchainVersion)
            || string.IsNullOrWhiteSpace(profile.Executable)
            || string.IsNullOrWhiteSpace(profile.OutputFile))
        {
            throw new InvalidOperationException(
                "Each Pkgsrv:Build:Profiles entry requires toolchain name/version, executable, and outputFile.");
        }

        if (!Path.IsPathFullyQualified(profile.Executable))
        {
            throw new InvalidOperationException(
                "Build profile executables must be absolute paths.");
        }

        if (Path.IsPathFullyQualified(profile.OutputFile)
            || profile.OutputFile.Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
                .Any(segment => segment == ".."))
        {
            throw new InvalidOperationException(
                "Build profile output files must be relative and remain inside the workspace.");
        }

        ParseNetworkMode(profile.NetworkMode);
    }

    private static BuildProfileNetworkMode ParseNetworkMode(string? value)
    {
        if (string.IsNullOrWhiteSpace(value)
            || value.Trim().Equals("Offline", StringComparison.OrdinalIgnoreCase))
        {
            return BuildProfileNetworkMode.Offline;
        }

        if (value.Trim().Equals("EgressProxy", StringComparison.OrdinalIgnoreCase))
        {
            return BuildProfileNetworkMode.EgressProxy;
        }

        throw new InvalidOperationException(
            "Build profile NetworkMode must be Offline or EgressProxy.");
    }
}
