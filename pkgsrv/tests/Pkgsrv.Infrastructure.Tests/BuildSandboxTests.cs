using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Security.Cryptography;
using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Application.Interfaces.Builds;
using Pkgsrv.Application.Interfaces.Sources;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Artifacts;
using Pkgsrv.Infrastructure.Builds;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

[SupportedOSPlatform("linux")]
public sealed class BuildSandboxTests
{
    private static readonly string[] ReadOnlyPaths = ["/usr", "/bin", "/lib", "/lib64"];

    [Fact]
    public async Task UnsupportedIsolation_ReturnsNotExecutedWithoutRunning()
    {
        using var harness = new SandboxHarness();
        var request = harness.CreateRequest(BuildIsolation.Container);

        var result = await harness.Sandbox.ExecuteAsync(request, CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public async Task MissingProfile_ReturnsNotExecuted()
    {
        using var harness = new SandboxHarness(profiles: []);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public async Task MissingBubblewrapRuntime_ReturnsNotExecuted()
    {
        using var harness = new SandboxHarness(bubblewrapPath: "/definitely/missing/bwrap");

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public async Task MissingWorkspaceRoot_IsRejectedWithoutCreatingOrRunning()
    {
        using var harness = new SandboxHarness(
            workspaceRoot: Path.Combine(Path.GetTempPath(), $"pkgsrv-missing-{Guid.NewGuid():N}"),
            createRoot: false);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.False(Directory.Exists(harness.Root));
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public async Task GroupOrWorldWritableWorkspaceRoot_IsRejected()
    {
        using var harness = new SandboxHarness();
        File.SetUnixFileMode(
            harness.Root,
            UnixFileMode.UserRead
            | UnixFileMode.UserWrite
            | UnixFileMode.UserExecute
            | UnixFileMode.GroupWrite
            | UnixFileMode.OtherWrite);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public async Task SymlinkedWorkspaceRoot_IsRejected()
    {
        using var harness = new SandboxHarness(createRoot: false);
        var target = Path.Combine(harness.Parent, "real-builds");
        Directory.CreateDirectory(target);
        File.SetUnixFileMode(
            target,
            UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
        Directory.CreateSymbolicLink(harness.Root, target);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public async Task RootServiceAccount_IsRejected()
    {
        using var harness = new SandboxHarness(isRootProvider: () => true);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public async Task WorkspaceRootWithWrongOwner_IsRejected()
    {
        using var harness = new SandboxHarness(ownerUidProvider: _ => uint.MaxValue);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public async Task PrecreatedChildCollision_IsRejectedWithoutRunning()
    {
        const string childName = "build-collision";
        using var harness = new SandboxHarness(childNameFactory: () => childName);
        var child = Path.Combine(harness.Root, childName);
        Directory.CreateDirectory(child);
        File.SetUnixFileMode(
            child,
            UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public async Task WorkspaceSwapAfterCheckout_IsRejectedBeforeBubblewrap()
    {
        using var harness = new SandboxHarness();
        var external = Path.Combine(Path.GetTempPath(), $"pkgsrv-swapped-{Guid.NewGuid():N}");
        Directory.CreateDirectory(external);
        harness.Source.AfterCheckout = workspace =>
        {
            Directory.Delete(workspace, recursive: true);
            Directory.CreateSymbolicLink(workspace, external);
        };

        try
        {
            var result = await harness.Sandbox.ExecuteAsync(
                harness.CreateRequest(),
                CancellationToken.None);

            Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
            Assert.Empty(harness.Runner.Requests);
            Assert.Equal(0, harness.ArtifactStore.StoreCalls);
        }
        finally
        {
            if (Directory.Exists(external))
            {
                Directory.Delete(external, recursive: true);
            }
        }
    }

    [Fact]
    public async Task NamespaceOnlyWithoutResourceLimitsRequiresOperatorAcceptance()
    {
        using var harness = new SandboxHarness(acceptNamespaceOnly: false);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Contains("explicitly accepted", result.Detail, StringComparison.Ordinal);
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public async Task SourceNotAllowed_ReturnsNotExecutedWithoutRunning()
    {
        using var harness = new SandboxHarness(sourceAllowed: false);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public async Task SandboxArguments_UseWorkspaceBackedPrivateTmpAndNoUnboundedTmpfs()
    {
        using var harness = new SandboxHarness();

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);
        var request = Assert.Single(harness.Runner.Requests);
        var arguments = request.Arguments;
        var workspace = WorkspaceFrom(request);

        Assert.Equal(BuildExecutionStatus.Completed, result.Status);
        Assert.DoesNotContain("--tmpfs", arguments);
        Assert.DoesNotContain("--share-net", arguments);
        Assert.Contains("--unshare-all", arguments);
        Assert.Contains("--cap-drop", arguments);
        Assert.Contains("ALL", arguments);

        var tmpBind = arguments
            .Select((argument, index) => (argument, index))
            .Single(pair => pair.argument == "--bind"
                && pair.index + 2 < arguments.Count
                && arguments[pair.index + 2] == "/tmp");
        var tmpSource = arguments[tmpBind.index + 1];

        Assert.NotEqual("/tmp", tmpSource);
        Assert.StartsWith(
            workspace + Path.DirectorySeparatorChar,
            tmpSource,
            StringComparison.Ordinal);
    }

    [Fact]
    public async Task OfflineProfile_NeverAddsShareNet()
    {
        using var harness = new SandboxHarness();

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(allowNetwork: false),
            CancellationToken.None);
        var buildRequest = Assert.Single(harness.Runner.Requests);

        Assert.Equal(BuildExecutionStatus.Completed, result.Status);
        Assert.Contains("--unshare-all", buildRequest.Arguments);
        Assert.DoesNotContain("--share-net", buildRequest.Arguments);
        Assert.Contains("/bin/cp", buildRequest.Arguments);
        Assert.DoesNotContain(
            buildRequest.Arguments,
            argument => argument.Contains("bash", StringComparison.OrdinalIgnoreCase)
                || argument.Equals("/bin/sh", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public async Task OfflineProfile_RejectsClientNetworkBeforeCheckoutOrRunner()
    {
        using var harness = new SandboxHarness();

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(allowNetwork: true),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Contains("offline", result.Detail, StringComparison.OrdinalIgnoreCase);
        Assert.Equal(0, harness.Source.Calls);
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public async Task NetworkEnabledProfile_IsRejectedBeforeCheckoutOrRunner()
    {
        using var harness = new SandboxHarness(
            profiles:
            [
                new BuildProfile(
                    "gcc",
                    "13.2.0",
                    "/bin/cp",
                    ["input.txt", "out/artifact.bin"],
                    "out/artifact.bin",
                    BuildProfileNetworkMode.EgressProxy)
            ]);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(allowNetwork: false),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Contains("network", result.Detail, StringComparison.OrdinalIgnoreCase);
        Assert.Equal(0, harness.Source.Calls);
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public async Task TimeoutFromRunner_ReturnsFailed()
    {
        using var harness = new SandboxHarness();
        harness.Runner.ResultFactory = _ => new BuildProcessResult(-1, string.Empty, string.Empty, true);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.Failed, result.Status);
        Assert.Contains("timeout", result.Detail, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task OutputTraversal_ReturnsFailedWithoutRunning()
    {
        using var harness = new SandboxHarness(
            profiles:
            [
                new BuildProfile(
                    "gcc",
                    "13.2.0",
                    "/bin/cp",
                    ["input.txt", "../outside"],
                    "../outside")
            ]);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.Failed, result.Status);
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public async Task BuildSuccess_StoresArtifactAndReturnsHash()
    {
        using var harness = new SandboxHarness();

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.Completed, result.Status);
        Assert.NotNull(result.ArtifactHash);
        var artifact = await harness.ArtifactStore.FindAsync(
            harness.Package.Key,
            result.ArtifactHash!,
            CancellationToken.None);
        Assert.NotNull(artifact);
        Assert.Equal(harness.OutputBytes, artifact.Bytes.ToArray());
        Assert.Empty(Directory.EnumerateDirectories(harness.Root));
    }

    [Fact]
    public async Task ExclusiveMkdir_SameNameConcurrentCreationHasOneWinner()
    {
        if (!OperatingSystem.IsLinux())
        {
            return;
        }

        using var harness = new SandboxHarness();
        var path = Path.Combine(harness.Root, "same-child");
        var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        var attempts = Enumerable.Range(0, 2)
            .Select(_ => Task.Run(async () =>
            {
                await gate.Task;
                return UnixFileMetadata.TryCreatePrivateDirectory(path);
            }))
            .ToArray();

        gate.SetResult();
        var results = await Task.WhenAll(attempts);

        Assert.Single(results, result => result == UnixFileMetadata.DirectoryCreationResult.Created);
        Assert.Single(results, result => result == UnixFileMetadata.DirectoryCreationResult.AlreadyExists);
        Directory.Delete(path);
    }

    [Fact]
    public async Task ConcurrentWorkspaceCreation_UsesDistinctChildrenAndCleansUp()
    {
        using var harness = new SandboxHarness();
        var secondRunner = new FakeBuildProcessRunner(harness.OutputBytes);
        var secondSandbox = new BubblewrapBuildSandbox(
            secondRunner,
            new FakeSourceCheckout(),
            harness.ArtifactStore,
            new TestSourceAllowlist(true),
            harness.Options);

        var results = await Task.WhenAll(
            harness.Sandbox.ExecuteAsync(harness.CreateRequest(), CancellationToken.None),
            secondSandbox.ExecuteAsync(harness.CreateRequest(), CancellationToken.None));

        Assert.All(results, result => Assert.Equal(BuildExecutionStatus.Completed, result.Status));
        var firstWorkspace = WorkspaceFrom(Assert.Single(harness.Runner.Requests));
        var secondWorkspace = WorkspaceFrom(Assert.Single(secondRunner.Requests));
        Assert.NotEqual(firstWorkspace, secondWorkspace);
        Assert.Empty(Directory.EnumerateDirectories(harness.Root));
    }

    [Fact]
    public async Task GroupWritableParentRuntimeDirectory_IsRejectedWithoutChmodRepair()
    {
        using var harness = new SandboxHarness();
        var unsafeMode = UnixFileMode.UserRead
            | UnixFileMode.UserWrite
            | UnixFileMode.UserExecute
            | UnixFileMode.GroupWrite;
        File.SetUnixFileMode(harness.Parent, unsafeMode);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Empty(harness.Runner.Requests);
        Assert.Equal(unsafeMode, File.GetUnixFileMode(harness.Parent));
    }

    [Fact]
    public async Task RealBubblewrapSmoke_RunsOnlyWhenRuntimeIsAvailable()
    {
        if (!File.Exists("/usr/bin/bwrap"))
        {
            return;
        }

        using var harness = new SandboxHarness(bubblewrapPath: "/usr/bin/bwrap");

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.Completed, result.Status);
        var artifact = await harness.ArtifactStore.FindAsync(
            harness.Package.Key,
            result.ArtifactHash!,
            CancellationToken.None);
        Assert.Equal(harness.OutputBytes, artifact!.Bytes.ToArray());
    }

    [Fact]
    public async Task OutputParentSymlink_IsRejectedBeforeArtifactStore()
    {
        using var harness = new SandboxHarness();
        var external = Path.Combine(
            Path.GetTempPath(),
            $"pkgsrv-external-output-{Guid.NewGuid():N}");
        Directory.CreateDirectory(external);
        harness.Runner.BeforeOutputWrite = workspace =>
        {
            var outputDirectory = Path.Combine(workspace, "out");
            Directory.Delete(outputDirectory, recursive: true);
            Directory.CreateSymbolicLink(outputDirectory, external);
        };

        try
        {
            var result = await harness.Sandbox.ExecuteAsync(
                harness.CreateRequest(),
                CancellationToken.None);

            Assert.Equal(BuildExecutionStatus.Failed, result.Status);
            Assert.Equal(0, harness.ArtifactStore.StoreCalls);
            Assert.Equal(0, harness.ArtifactStore.FindCalls);
        }
        finally
        {
            if (Directory.Exists(external))
            {
                Directory.Delete(external, recursive: true);
            }
        }
    }

    [Fact]
    public async Task OversizedBuildOutput_IsRejectedBeforeReadOrStore()
    {
        using var harness = new SandboxHarness(maxArtifactBytes: 4);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.Failed, result.Status);
        Assert.Equal(0, harness.ArtifactStore.StoreCalls);
        Assert.Equal(0, harness.ArtifactStore.FindCalls);
    }

    [Fact]
    public async Task MissingWorkspaceQuota_IsRejectedWithoutRunning()
    {
        using var harness = new SandboxHarness(workspaceQuotaBytes: 0);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Contains("quota", result.Detail, StringComparison.OrdinalIgnoreCase);
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public async Task NonDurableArtifactStore_IsRejectedBeforeCheckoutOrRunner()
    {
        using var harness = new SandboxHarness(requireDurableArtifactStore: true);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Contains("durable artifact", result.Detail, StringComparison.OrdinalIgnoreCase);
        Assert.Empty(harness.Runner.Requests);
        Assert.Equal(0, harness.ArtifactStore.StoreCalls);
    }

    [Fact]
    public async Task ArtifactCapacity_IsRejectedBeforeCheckoutOrRunner()
    {
        using var harness = new SandboxHarness();
        var durableStore = new DurableTrackingArtifactStore();
        var options = harness.Options with
        {
            RequireDurableArtifactStore = true,
            ArtifactRoot = harness.Root,
            ArtifactQuotaBytes = 1024L * 1024 * 1024,
            ArtifactQuotaInodes = 1_000_000,
            RequiredArtifactBytes = 1024 * 1024
        };
        var sandbox = new BubblewrapBuildSandbox(
            harness.Runner,
            harness.Source,
            durableStore,
            new TestSourceAllowlist(true),
            options);

        var result = await sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Contains("Artifact capacity", result.Detail, StringComparison.OrdinalIgnoreCase);
        Assert.Equal(0, harness.Source.Calls);
        Assert.Empty(harness.Runner.Requests);
        Assert.Equal(0, durableStore.StoreCalls);
    }

    [Fact]
    public async Task MissingWorkspaceQuotaInodes_IsRejectedWithoutRunning()
    {
        using var harness = new SandboxHarness(workspaceQuotaInodes: 0);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Contains("inode", result.Detail, StringComparison.OrdinalIgnoreCase);
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public void WorkspaceQuotaValidator_RejectsRamfs()
    {
        Assert.False(WorkspaceFilesystemQuota.MeetsCaps(
            "ramfs",
            1024,
            10,
            2048,
            20));
    }

    [Fact]
    public void WorkspaceQuotaValidator_RejectsTooManyInodes()
    {
        Assert.False(WorkspaceFilesystemQuota.MeetsCaps(
            "tmpfs",
            1024,
            21,
            2048,
            20));
    }

    [Fact]
    public void WorkspaceQuotaValidator_RejectsMissingInodeCap()
    {
        Assert.False(WorkspaceFilesystemQuota.MeetsCaps(
            "tmpfs",
            1024,
            10,
            2048,
            0));
    }

    [Fact]
    public void WorkspaceQuotaValidator_AcceptsTmpfsWithinBothCaps()
    {
        Assert.True(WorkspaceFilesystemQuota.MeetsCaps(
            "tmpfs",
            1024,
            10,
            2048,
            20));
    }

    [Fact]
    public async Task WorkspaceRootNotOnCappedFilesystem_IsRejectedWithoutRunning()
    {
        using var harness = new SandboxHarness(workspaceQuotaValidator: (_, _, _) => false);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.NotExecuted, result.Status);
        Assert.Contains("tmpfs", result.Detail, StringComparison.OrdinalIgnoreCase);
        Assert.Empty(harness.Runner.Requests);
    }

    [Fact]
    public async Task FifoOutput_IsRejectedWithoutHangingOrStoring()
    {
        using var harness = new SandboxHarness();
        harness.Runner.CreateFifoOutput = true;

        var result = await harness.Sandbox
            .ExecuteAsync(harness.CreateRequest(), CancellationToken.None)
            .WaitAsync(TimeSpan.FromSeconds(10));

        Assert.Equal(BuildExecutionStatus.Failed, result.Status);
        Assert.Contains("regular file", result.Detail, StringComparison.OrdinalIgnoreCase);
        Assert.Equal(0, harness.ArtifactStore.StoreCalls);
        Assert.Empty(Directory.EnumerateDirectories(harness.Root));
    }

    [Fact]
    public async Task HashMismatch_IsRejectedBeforeArtifactStore()
    {
        using var harness = new SandboxHarness();
        var wrongHash = ContentHash.Create(new string('a', 64));

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(expectedArtifactHash: wrongHash),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.Failed, result.Status);
        Assert.Contains("hash", result.Detail, StringComparison.OrdinalIgnoreCase);
        Assert.Equal(0, harness.ArtifactStore.StoreCalls);
        Assert.Empty(Directory.EnumerateDirectories(harness.Root));
    }

    [Fact]
    public async Task WorkspaceChmodByBuild_IsRepairedForCleanup()
    {
        using var harness = new SandboxHarness();
        harness.Runner.AfterOutputWrite = workspace =>
            File.SetUnixFileMode(
                workspace,
                UnixFileMode.UserRead | UnixFileMode.UserExecute);

        var result = await harness.Sandbox.ExecuteAsync(
            harness.CreateRequest(),
            CancellationToken.None);

        Assert.Equal(BuildExecutionStatus.Completed, result.Status);
        Assert.Empty(Directory.EnumerateDirectories(harness.Root));
    }

    [Fact]
    public void WorkspaceQuotaValidator_RejectsNonMountPath()
    {
        var path = Path.Combine(Path.GetTempPath(), $"pkgsrv-quota-{Guid.NewGuid():N}");
        Directory.CreateDirectory(path);

        try
        {
            Assert.False(WorkspaceFilesystemQuota.IsCapped(
                path,
                1024L * 1024 * 1024,
                1_000_000));
        }
        finally
        {
            Directory.Delete(path);
        }
    }

    private sealed class SandboxHarness : IDisposable
    {
        private readonly string _parent;
        private readonly string _root;

        public SandboxHarness(
            IReadOnlyList<BuildProfile>? profiles = null,
            bool sourceAllowed = true,
            string bubblewrapPath = "/usr/bin/bwrap",
            long maxArtifactBytes = 256L * 1024 * 1024,
            long workspaceQuotaBytes = 1024L * 1024 * 1024,
            long workspaceQuotaInodes = 1_000_000,
            bool requireDurableArtifactStore = false,
            Func<string, long, long, bool>? workspaceQuotaValidator = null,
            bool acceptNamespaceOnly = true,
            Func<string, uint>? ownerUidProvider = null,
            Func<bool>? isRootProvider = null,
            Func<string>? childNameFactory = null,
            string? workspaceRoot = null,
            bool createRoot = true)
        {
            _parent = Path.Combine(Path.GetTempPath(), $"pkgsrv-build-parent-{Guid.NewGuid():N}");
            Directory.CreateDirectory(_parent);
            File.SetUnixFileMode(
                _parent,
                UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);

            _root = workspaceRoot ?? Path.Combine(_parent, "builds");

            if (createRoot)
            {
                Directory.CreateDirectory(_root);
                File.SetUnixFileMode(
                    _root,
                    UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
            }

            OutputBytes = "sandbox-output"u8.ToArray();
            var contentHash = Convert.ToHexString(SHA256.HashData(OutputBytes)).ToLowerInvariant();
            Package = Package.Register(
                TestPackages.CreateManifest(contentHash: contentHash),
                BuildPolicy.Create(BuildIsolation.Namespace, false, TimeSpan.FromSeconds(30)),
                TestPackages.Timestamp);
            Runner = new FakeBuildProcessRunner(OutputBytes);
            Source = new FakeSourceCheckout();
            ArtifactStore = new TrackingArtifactStore();
            Options = new BuildSandboxOptions(
                bubblewrapPath,
                _root,
                profiles ??
                [
                    new BuildProfile(
                        "gcc",
                        "13.2.0",
                        "/bin/cp",
                        ["input.txt", "out/artifact.bin"],
                        "out/artifact.bin")
                ],
                ReadOnlyPaths,
                maxArtifactBytes,
                workspaceQuotaBytes,
                workspaceQuotaInodes,
                AcceptNamespaceOnlyWithoutResourceLimits: acceptNamespaceOnly,
                RequireDurableArtifactStore: requireDurableArtifactStore,
                WorkspaceQuotaValidator: workspaceQuotaValidator ?? ((_, _, _) => true));
            Sandbox = new BubblewrapBuildSandbox(
                Runner,
                Source,
                ArtifactStore,
                new TestSourceAllowlist(sourceAllowed),
                Options,
                ownerUidProvider,
                isRootProvider: isRootProvider,
                childNameFactory: childNameFactory);
        }

        public string Parent => _parent;

        public string Root => _root;

        public BuildSandboxOptions Options { get; }

        public Package Package { get; }

        public byte[] OutputBytes { get; }

        public FakeBuildProcessRunner Runner { get; }

        public FakeSourceCheckout Source { get; }

        public TrackingArtifactStore ArtifactStore { get; }

        public BubblewrapBuildSandbox Sandbox { get; }

        public BuildSandboxRequest CreateRequest(
            BuildIsolation isolation = BuildIsolation.Namespace,
            bool allowNetwork = false,
            ContentHash? expectedArtifactHash = null)
        {
            var policy = BuildPolicy.Create(isolation, allowNetwork, TimeSpan.FromSeconds(30));

            return new BuildSandboxRequest(
                Package.Key,
                Package.Manifest.Source,
                policy,
                Package.Manifest.Toolchain,
                expectedArtifactHash ?? Package.Manifest.ContentHash);
        }

        public void Dispose()
        {
            if (Directory.Exists(_parent))
            {
                Directory.Delete(_parent, recursive: true);
            }
        }
    }

    private sealed class FakeBuildProcessRunner : IBuildProcessRunner
    {
        private readonly byte[] _output;

        public FakeBuildProcessRunner(byte[] output)
        {
            _output = output;
        }

        public List<BuildProcessRequest> Requests { get; } = new();

        public Action<string>? BeforeOutputWrite { get; set; }

        public Action<string>? AfterOutputWrite { get; set; }

        public bool CreateFifoOutput { get; set; }

        public Func<BuildProcessRequest, BuildProcessResult> ResultFactory { get; set; } =
            request => new BuildProcessResult(0, string.Empty, string.Empty, false);

        public Task<BuildProcessResult> RunAsync(
            BuildProcessRequest request,
            CancellationToken cancellationToken)
        {
            Requests.Add(request);
            var bindIndex = request.Arguments
                .Select((argument, index) => (argument, index))
                .First(pair => pair.argument == "--bind"
                    && pair.index + 2 < request.Arguments.Count
                    && request.Arguments[pair.index + 2] == "/source")
                .index;
            var workspace = request.Arguments[bindIndex + 1];
            BeforeOutputWrite?.Invoke(workspace);
            Directory.CreateDirectory(Path.Combine(workspace, "out"));
            var outputPath = Path.Combine(workspace, "out", "artifact.bin");

            if (CreateFifoOutput)
            {
                Assert.Equal(0, mkfifo(outputPath, 0x180));
            }
            else
            {
                File.WriteAllBytes(outputPath, _output);
            }

            AfterOutputWrite?.Invoke(workspace);
            return Task.FromResult(ResultFactory(request));
        }

        [DllImport("libc", EntryPoint = "mkfifo", CharSet = CharSet.Ansi, SetLastError = true)]
        private static extern int mkfifo(string path, uint mode);
    }

    private sealed class TrackingArtifactStore : IArtifactStore
    {
        private readonly InMemoryArtifactStore _inner = new();

        public int StoreCalls { get; private set; }

        public int FindCalls { get; private set; }

        public Task<StoredArtifact> StoreAsync(
            PackageKey package,
            ReadOnlyMemory<byte> bytes,
            CancellationToken cancellationToken)
        {
            StoreCalls++;
            return _inner.StoreAsync(package, bytes, cancellationToken);
        }

        public Task<ArtifactContent?> FindAsync(
            PackageKey package,
            ContentHash contentHash,
            CancellationToken cancellationToken)
        {
            FindCalls++;
            return _inner.FindAsync(package, contentHash, cancellationToken);
        }
    }

    private sealed class DurableTrackingArtifactStore : IDurableArtifactStore
    {
        private readonly InMemoryArtifactStore _inner = new();

        public int StoreCalls { get; private set; }

        public Task<StoredArtifact> StoreAsync(
            PackageKey package,
            ReadOnlyMemory<byte> bytes,
            CancellationToken cancellationToken)
        {
            StoreCalls++;
            return _inner.StoreAsync(package, bytes, cancellationToken);
        }

        public Task<ArtifactContent?> FindAsync(
            PackageKey package,
            ContentHash contentHash,
            CancellationToken cancellationToken) =>
            _inner.FindAsync(package, contentHash, cancellationToken);
    }

    private sealed class FakeSourceCheckout : ISourceCheckout
    {
        public Action<string>? AfterCheckout { get; set; }

        public int Calls { get; private set; }

        public Task CheckoutAsync(
            PackageSource source,
            string workspace,
            TimeSpan timeout,
            CancellationToken cancellationToken)
        {
            Calls++;
            File.WriteAllText(Path.Combine(workspace, "input.txt"), "source");
            AfterCheckout?.Invoke(workspace);
            return Task.CompletedTask;
        }
    }

    private static string WorkspaceFrom(BuildProcessRequest request)
    {
        var bindIndex = request.Arguments
            .Select((argument, index) => (argument, index))
            .First(pair => pair.argument == "--bind"
                && pair.index + 2 < request.Arguments.Count
                && request.Arguments[pair.index + 2] == "/source")
            .index;
        return request.Arguments[bindIndex + 1];
    }

    private sealed class TestSourceAllowlist : ISourceAllowlist
    {
        private readonly bool _allowed;

        public TestSourceAllowlist(bool allowed)
        {
            _allowed = allowed;
        }

        public bool IsAllowed(PackageSource source) => _allowed;
    }
}
