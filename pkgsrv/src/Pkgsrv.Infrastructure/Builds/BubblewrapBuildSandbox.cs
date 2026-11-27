using System.Security.Cryptography;
using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Application.Interfaces.Builds;
using Pkgsrv.Application.Interfaces.Sources;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Artifacts;

namespace Pkgsrv.Infrastructure.Builds;

public sealed class BubblewrapBuildSandbox : IBuildSandbox
{
    private static readonly IReadOnlyDictionary<string, string> SandboxEnvironment =
        new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["PATH"] = "/usr/bin:/bin",
            ["HOME"] = "/tmp",
            ["TMPDIR"] = "/tmp",
            ["LC_ALL"] = "C"
        };

    private static readonly UnixFileMode PrivateDirectoryMode =
        UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute;

    private static readonly TimeSpan OutputReadTimeout = TimeSpan.FromSeconds(30);

    private readonly IBuildProcessRunner _processRunner;
    private readonly ISourceCheckout _sourceCheckout;
    private readonly IArtifactStore _artifactStore;
    private readonly ISourceAllowlist _sourceAllowlist;
    private readonly BuildSandboxOptions _options;
    private readonly Func<string, uint> _ownerUidProvider;
    private readonly Func<uint> _effectiveUidProvider;
    private readonly Func<bool> _isRootProvider;
    private readonly Func<string> _childNameFactory;

    public BubblewrapBuildSandbox(
        IBuildProcessRunner processRunner,
        ISourceCheckout sourceCheckout,
        IArtifactStore artifactStore,
        ISourceAllowlist sourceAllowlist,
        BuildSandboxOptions options,
        Func<string, uint>? ownerUidProvider = null,
        Func<uint>? effectiveUidProvider = null,
        Func<bool>? isRootProvider = null,
        Func<string>? childNameFactory = null)
    {
        _processRunner = processRunner;
        _sourceCheckout = sourceCheckout;
        _artifactStore = artifactStore;
        _sourceAllowlist = sourceAllowlist;
        _options = options;
        _ownerUidProvider = ownerUidProvider ?? UnixFileMetadata.GetOwnerUid;
        _effectiveUidProvider = effectiveUidProvider ?? UnixFileMetadata.GetEffectiveUid;
        _isRootProvider = isRootProvider ?? DefaultIsRoot;
        _childNameFactory = childNameFactory ?? (() => $"build-{Guid.NewGuid():N}");
    }

    public async Task<BuildSandboxResult> ExecuteAsync(
        BuildSandboxRequest request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);

        if (request.Policy.Isolation != BuildIsolation.Namespace)
        {
            return BuildSandboxResult.NotExecuted(
                $"Bubblewrap supports only Namespace isolation; requested {request.Policy.Isolation}.");
        }

        if (!_options.AcceptNamespaceOnlyWithoutResourceLimits)
        {
            return BuildSandboxResult.NotExecuted(
                "Namespace-only bubblewrap execution without cgroup CPU/memory limits has not been explicitly accepted by the operator.");
        }

        if (_options.RequireDurableArtifactStore
            && _artifactStore is not IDurableArtifactStore)
        {
            return BuildSandboxResult.NotExecuted(
                "Build execution requires durable artifact storage; configure a pre-provisioned private Pkgsrv:Artifacts:Root.");
        }

        if (_options.RequireDurableArtifactStore
            && !ArtifactRootValidator.TryValidate(
                _options.ArtifactRoot ?? string.Empty,
                _options.ArtifactQuotaBytes,
                _options.ArtifactQuotaInodes,
                _options.RequiredArtifactBytes,
                out var artifactRootDetail))
        {
            return BuildSandboxResult.NotExecuted(
                $"Artifact capacity admission failed before checkout: {artifactRootDetail}");
        }

        if (!OperatingSystem.IsLinux() || _isRootProvider())
        {
            return BuildSandboxResult.NotExecuted(
                "Build execution requires a non-root Linux service account.");
        }

        if (request.Toolchain is null)
        {
            return BuildSandboxResult.NotExecuted("Build request has no toolchain/profile.");
        }

        if (!_sourceAllowlist.IsAllowed(request.Source))
        {
            return BuildSandboxResult.NotExecuted("Source is no longer on the package source allowlist.");
        }

        var profile = _options.Profiles.FirstOrDefault(candidate => candidate.Matches(request.Toolchain));

        if (profile is null)
        {
            return BuildSandboxResult.NotExecuted(
                $"No allowlisted build profile exists for toolchain {request.Toolchain.Name} {request.Toolchain.Version}.");
        }

        if (profile.NetworkMode == BuildProfileNetworkMode.Offline)
        {
            if (request.Policy.AllowNetwork)
            {
                return BuildSandboxResult.NotExecuted(
                    "The allowlisted build profile is offline; the request cannot enable network access.");
            }
        }
        else
        {
            return BuildSandboxResult.NotExecuted(
                "Network-enabled build profiles require an enforceable egress-proxy network namespace policy; none is implemented.");
        }

        if (!File.Exists(_options.BubblewrapPath))
        {
            return BuildSandboxResult.NotExecuted(
                "Bubblewrap runtime is not available at the configured path.");
        }

        if (!Path.IsPathFullyQualified(_options.WorkspaceRoot))
        {
            return BuildSandboxResult.Failed("Configured build workspace root must be absolute.");
        }

        if (!Path.IsPathFullyQualified(profile.Executable))
        {
            return BuildSandboxResult.Failed("Configured build executable must be absolute.");
        }

        if (!IsSafeRelativePath(profile.OutputFile))
        {
            return BuildSandboxResult.Failed("Configured build output path must stay inside the workspace.");
        }

        var workspaceRoot = Path.GetFullPath(_options.WorkspaceRoot);

        if (!TryValidateWorkspaceRoot(workspaceRoot))
        {
            return BuildSandboxResult.NotExecuted(
                "Configured build workspace root is missing or does not meet private ownership, mode, and symlink requirements.");
        }

        if (_options.WorkspaceQuotaBytes <= 0 || _options.WorkspaceQuotaInodes <= 0)
        {
            return BuildSandboxResult.NotExecuted(
                "Build execution requires explicit bounded workspace quotas for bytes and inodes.");
        }

        var quotaValidator = _options.WorkspaceQuotaValidator ?? WorkspaceFilesystemQuota.IsCapped;

        if (!quotaValidator(
                workspaceRoot,
                _options.WorkspaceQuotaBytes,
                _options.WorkspaceQuotaInodes))
        {
            return BuildSandboxResult.NotExecuted(
                "Build workspace root must be a dedicated tmpfs mount whose total size and inode count do not exceed the configured quotas.");
        }

        if (!TryCreateWorkspace(workspaceRoot, out var workspace))
        {
            return BuildSandboxResult.NotExecuted(
                "Unable to create a private, non-conflicting build workspace.");
        }

        BuildSandboxResult result;
        var cleanedUp = false;

        try
        {
            result = await ExecuteInWorkspaceAsync(
                request,
                profile,
                workspaceRoot,
                workspace,
                cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            cleanedUp = TryDeleteWorkspace(workspaceRoot, workspace);
        }

        if (!cleanedUp)
        {
            return BuildSandboxResult.Failed(
                "Build workspace cleanup failed; execution is failed closed because the private workspace could not be removed.");
        }

        return result;
    }

    private async Task<BuildSandboxResult> ExecuteInWorkspaceAsync(
        BuildSandboxRequest request,
        BuildProfile profile,
        string workspaceRoot,
        string workspace,
        CancellationToken cancellationToken)
    {
        try
        {
            if (!TryValidateWorkspaceForUse(workspaceRoot, workspace))
            {
                return BuildSandboxResult.NotExecuted(
                    "Build workspace failed private ownership or symlink validation before checkout.");
            }

            await _sourceCheckout.CheckoutAsync(
                request.Source,
                workspace,
                request.Policy.Timeout,
                cancellationToken).ConfigureAwait(false);

            if (!TryValidateWorkspaceForUse(workspaceRoot, workspace))
            {
                return BuildSandboxResult.NotExecuted(
                    "Build workspace failed private ownership or symlink validation before sandbox execution.");
            }

            Directory.CreateDirectory(Path.Combine(workspace, "out"));

            if (!TryCreateSandboxTempDirectory(workspace, out var sandboxTemp))
            {
                return BuildSandboxResult.Failed(
                    "A private sandbox temp directory could not be created inside the capped workspace.");
            }

            var arguments = BuildBubblewrapArguments(request, profile, workspace, sandboxTemp);
            var processResult = await _processRunner.RunAsync(
                new BuildProcessRequest(
                    _options.BubblewrapPath,
                    arguments,
                    workspaceRoot,
                    SandboxEnvironment,
                    request.Policy.Timeout),
                cancellationToken).ConfigureAwait(false);

            if (processResult.TimedOut)
            {
                return BuildSandboxResult.Failed("Build exceeded the configured timeout.");
            }

            if (processResult.ExitCode != 0)
            {
                return BuildSandboxResult.Failed(
                    $"Build process failed with exit code {processResult.ExitCode}.");
            }

            var outputPath = ResolveOutputPath(workspace, profile.OutputFile);

            if (outputPath is null)
            {
                return BuildSandboxResult.Failed(
                    "Build output was missing or escaped the workspace.");
            }

            var read = ReadRegularOutput(outputPath, cancellationToken);

            if (!read.Success)
            {
                return BuildSandboxResult.Failed(read.Detail);
            }

            var actualHash = Convert.ToHexString(
                SHA256.HashData(read.Bytes)).ToLowerInvariant();

            if (request.ExpectedArtifactHash is not null
                && !string.Equals(
                    request.ExpectedArtifactHash.Value,
                    actualHash,
                    StringComparison.Ordinal))
            {
                return BuildSandboxResult.Failed(
                    "Built artifact hash does not match the registered package content hash.");
            }

            var stored = await _artifactStore
                .StoreAsync(request.Package, read.Bytes, cancellationToken)
                .ConfigureAwait(false);

            if (request.ExpectedArtifactHash is not null
                && !string.Equals(
                    request.ExpectedArtifactHash.Value,
                    stored.ContentHash.Value,
                    StringComparison.Ordinal))
            {
                return BuildSandboxResult.Failed(
                    "Stored artifact hash does not match the registered package content hash.");
            }

            return new BuildSandboxResult(
                BuildExecutionStatus.Completed,
                "Sandboxed build completed and artifact was stored content-addressably.",
                stored.ContentHash);
        }
        catch (OperationCanceledException)
        {
            return BuildSandboxResult.Failed("Build cancellation was requested.");
        }
        catch (Exception exception)
        {
            return BuildSandboxResult.Failed($"Sandboxed build failed: {exception.Message}");
        }
    }

    private OutputReadResult ReadRegularOutput(
        string outputPath,
        CancellationToken cancellationToken)
    {
        if (!UnixFileMetadata.TryOpenRegularFile(outputPath, out var handle, out var length)
            || handle is null)
        {
            return OutputReadResult.Failed(
                "Build output is not a regular file; FIFOs, devices, and symlinks are rejected.");
        }

        if (length > _options.MaxArtifactBytes)
        {
            handle.Dispose();
            return OutputReadResult.Failed(
                "Build output exceeds the configured maximum artifact size.");
        }

        try
        {
            using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            deadline.CancelAfter(OutputReadTimeout);
            using var stream = new FileStream(
                handle,
                FileAccess.Read,
                bufferSize: 81920,
                isAsync: false);

            var bytes = new byte[length];
            var offset = 0;

            while (offset < bytes.Length)
            {
                deadline.Token.ThrowIfCancellationRequested();
                var read = stream.Read(bytes, offset, bytes.Length - offset);

                if (read == 0)
                {
                    return OutputReadResult.Failed(
                        "Build output changed size while being read.");
                }

                offset += read;
            }

            return OutputReadResult.FromBytes(bytes);
        }
        catch (OperationCanceledException)
        {
            return OutputReadResult.Failed("Build output read timed out.");
        }
        catch (IOException)
        {
            return OutputReadResult.Failed("Build output could not be read safely.");
        }
    }

    private sealed record OutputReadResult(bool Success, byte[] Bytes, string Detail)
    {
        public static OutputReadResult FromBytes(byte[] bytes) =>
            new(true, bytes, string.Empty);

        public static OutputReadResult Failed(string detail) =>
            new(false, Array.Empty<byte>(), detail);
    }

    private bool TryCreateSandboxTempDirectory(string workspace, out string sandboxTemp)
    {
        sandboxTemp = string.Empty;

        for (var attempt = 0; attempt < 5; attempt++)
        {
            var candidate = Path.Combine(workspace, $".tmp-{Guid.NewGuid():N}");

            if (UnixFileMetadata.TryCreatePrivateDirectory(candidate)
                != UnixFileMetadata.DirectoryCreationResult.Created)
            {
                continue;
            }

            if (IsPrivateDirectory(candidate) && IsUnderRoot(workspace, candidate))
            {
                sandboxTemp = candidate;
                return true;
            }

            return false;
        }

        return false;
    }

    private IReadOnlyList<string> BuildBubblewrapArguments(
        BuildSandboxRequest request,
        BuildProfile profile,
        string workspace,
        string sandboxTemp)
    {
        var arguments = new List<string>
        {
            "--die-with-parent",
            "--new-session",
            "--unshare-all",
            "--cap-drop", "ALL"
        };

        arguments.AddRange(
        [
            "--clearenv",
            "--setenv", "PATH", "/usr/bin:/bin",
            "--setenv", "HOME", "/tmp",
            "--setenv", "TMPDIR", "/tmp",
            "--proc", "/proc",
            "--dev", "/dev"
        ]);

        foreach (var path in _options.ReadOnlyPaths.Distinct(StringComparer.Ordinal))
        {
            if (File.Exists(path) || Directory.Exists(path))
            {
                arguments.AddRange(["--ro-bind", path, path]);
            }
        }

        arguments.AddRange(["--bind", sandboxTemp, "/tmp"]);
        arguments.AddRange(["--bind", workspace, "/source", "--chdir", "/source", "--"]);
        arguments.Add(profile.Executable);
        arguments.AddRange(profile.Arguments);

        return arguments;
    }

    private static string? ResolveOutputPath(string workspace, string outputFile)
    {
        var workspaceFull = Path.GetFullPath(workspace);
        var fullPath = Path.GetFullPath(Path.Combine(workspaceFull, outputFile));
        var relative = Path.GetRelativePath(workspaceFull, fullPath);

        if (relative == ".."
            || relative.StartsWith($"..{Path.DirectorySeparatorChar}", StringComparison.Ordinal)
            || Path.IsPathRooted(relative)
            || IsReparsePoint(workspaceFull))
        {
            return null;
        }

        var current = workspaceFull;

        foreach (var segment in relative.Split(
            Path.DirectorySeparatorChar,
            Path.AltDirectorySeparatorChar))
        {
            if (string.IsNullOrEmpty(segment) || segment == ".")
            {
                continue;
            }

            current = Path.Combine(current, segment);

            if (!File.Exists(current) && !Directory.Exists(current))
            {
                return null;
            }

            if (IsReparsePoint(current))
            {
                return null;
            }
        }

        return File.Exists(fullPath) ? fullPath : null;
    }

    private static bool IsSafeRelativePath(string path) =>
        !string.IsNullOrWhiteSpace(path)
        && !Path.IsPathFullyQualified(path)
        && !path.Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
            .Any(segment => segment == "..");

    private static bool IsReparsePoint(string path)
    {
        var attributes = File.GetAttributes(path);

        if ((attributes & FileAttributes.ReparsePoint) != 0)
        {
            return true;
        }

        return attributes.HasFlag(FileAttributes.Directory)
            ? new DirectoryInfo(path).LinkTarget is not null
            : new FileInfo(path).LinkTarget is not null;
    }

    private bool TryValidateWorkspaceRoot(string root)
    {
        if (!OperatingSystem.IsLinux()
            || !Directory.Exists(root)
            || IsReparsePoint(root)
            || !ValidatePathComponents(root))
        {
            return false;
        }

        if (!IsPrivateDirectory(root))
        {
            return false;
        }

        var parent = Directory.GetParent(root)?.FullName;

        if (parent is null
            || !Directory.Exists(parent)
            || IsReparsePoint(parent)
            || !IsOwnedByService(parent))
        {
            return false;
        }

        try
        {
            var parentMode = File.GetUnixFileMode(parent);
            return (parentMode & (UnixFileMode.GroupWrite | UnixFileMode.OtherWrite)) == 0;
        }
        catch (IOException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
    }

    private bool ValidatePathComponents(string path)
    {
        var root = Path.GetPathRoot(path);

        if (string.IsNullOrEmpty(root)
            || !Directory.Exists(root)
            || IsReparsePoint(root))
        {
            return false;
        }

        var current = root;
        var relative = Path.GetRelativePath(root, path);

        foreach (var segment in relative.Split(
            Path.DirectorySeparatorChar,
            Path.AltDirectorySeparatorChar))
        {
            if (string.IsNullOrEmpty(segment) || segment == ".")
            {
                continue;
            }

            current = Path.Combine(current, segment);

            if (!Directory.Exists(current) || IsReparsePoint(current))
            {
                return false;
            }
        }

        return true;
    }

    private bool TryCreateWorkspace(string root, out string workspace)
    {
        workspace = string.Empty;

        if (!OperatingSystem.IsLinux())
        {
            return false;
        }

        for (var attempt = 0; attempt < 5; attempt++)
        {
            var name = _childNameFactory();

            if (!IsSimpleChildName(name))
            {
                continue;
            }

            var candidate = Path.Combine(root, name);

            if (Directory.Exists(candidate) || File.Exists(candidate))
            {
                continue;
            }

            try
            {
                var creation = UnixFileMetadata.TryCreatePrivateDirectory(
                    candidate);

                if (creation == UnixFileMetadata.DirectoryCreationResult.AlreadyExists)
                {
                    continue;
                }

                if (creation != UnixFileMetadata.DirectoryCreationResult.Created)
                {
                    continue;
                }
            }
            catch (UnauthorizedAccessException)
            {
                continue;
            }

            if (!IsPrivateDirectory(candidate)
                || !IsUnderRoot(root, candidate))
            {
                TryDeleteWorkspace(root, candidate);
                continue;
            }

            workspace = candidate;
            return true;
        }

        return false;
    }

    private bool TryValidateWorkspaceForUse(string root, string workspace) =>
        TryValidateWorkspaceRoot(root)
        && Directory.Exists(workspace)
        && !IsReparsePoint(workspace)
        && IsPrivateDirectory(workspace)
        && IsUnderRoot(root, workspace);

    private bool IsPrivateDirectory(string path)
    {
        if (!OperatingSystem.IsLinux())
        {
            return false;
        }

        try
        {
            return Directory.Exists(path)
                && !IsReparsePoint(path)
                && File.GetUnixFileMode(path) == PrivateDirectoryMode
                && IsOwnedByService(path);
        }
        catch (IOException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
    }

    private bool IsOwnedByService(string path)
    {
        try
        {
            return _ownerUidProvider(path) == _effectiveUidProvider();
        }
        catch (IOException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static bool IsSimpleChildName(string name) =>
        !string.IsNullOrWhiteSpace(name)
        && name != "."
        && name != ".."
        && Path.GetFileName(name) == name
        && name.IndexOfAny(Path.GetInvalidFileNameChars()) < 0;

    private static bool IsUnderRoot(string root, string path)
    {
        var fullRoot = Path.GetFullPath(root);
        var fullPath = Path.GetFullPath(path);
        var relative = Path.GetRelativePath(fullRoot, fullPath);

        return relative != "."
            && relative != ".."
            && !relative.StartsWith($"..{Path.DirectorySeparatorChar}", StringComparison.Ordinal)
            && !Path.IsPathRooted(relative);
    }

    private bool DefaultIsRoot()
    {
        var uid = _effectiveUidProvider();
        return uid == 0 || uid == uint.MaxValue;
    }

    private bool TryDeleteWorkspace(string root, string workspace)
    {
        if (!OperatingSystem.IsLinux()
            || !IsUnderRoot(root, workspace)
            || !TryValidateWorkspaceRoot(root))
        {
            return false;
        }

        try
        {
            if (!Directory.Exists(workspace))
            {
                return !File.Exists(workspace);
            }

            if (IsReparsePoint(workspace))
            {
                File.Delete(workspace);
                return !File.Exists(workspace);
            }

            return TryDeleteTree(workspace);
        }
        catch (IOException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static bool TryDeleteTree(string directory)
    {
        if (!OperatingSystem.IsLinux())
        {
            return false;
        }

        try
        {
            if (IsReparsePoint(directory))
            {
                File.Delete(directory);
                return true;
            }

            File.SetUnixFileMode(directory, PrivateDirectoryMode);

            foreach (var entry in Directory.EnumerateFileSystemEntries(directory))
            {
                if (UnixFileMetadata.IsSymbolicLink(entry))
                {
                    File.Delete(entry);
                    continue;
                }

                if (Directory.Exists(entry))
                {
                    if (!TryDeleteTree(entry))
                    {
                        return false;
                    }

                    continue;
                }

                try
                {
                    File.SetUnixFileMode(
                        entry,
                        UnixFileMode.UserRead | UnixFileMode.UserWrite);
                }
                catch (IOException)
                {
                }
                catch (UnauthorizedAccessException)
                {
                }

                File.Delete(entry);
            }

            Directory.Delete(directory, recursive: false);
            return true;
        }
        catch (IOException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
    }
}
