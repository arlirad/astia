using System.Runtime.Versioning;
using System.Security.Cryptography;
using Microsoft.Extensions.DependencyInjection;
using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Application.Interfaces.Builds;
using Pkgsrv.Infrastructure.Artifacts;
using Pkgsrv.Infrastructure.Builds;

namespace Pkgsrv.Presentation.Tests;

[SupportedOSPlatform("linux")]
public sealed class PkgsrvCapabilitiesCompositionTests
{
    [Fact]
    public void BuildEnabledWithoutArtifactRoot_FailsBeforeHostStarts()
    {
        var root = Path.Combine(Path.GetTempPath(), $"pkgsrv-no-artifacts-{Guid.NewGuid():N}");
        var buildRoot = Path.Combine(root, "builds");
        Directory.CreateDirectory(root);
        Directory.CreateDirectory(buildRoot);
        File.SetUnixFileMode(
            buildRoot,
            UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);

        try
        {
            Assert.Throws<InvalidOperationException>(() => PkgsrvHost.Create(
            [
                "--Pkgsrv:Build:Enabled=true",
                "--Pkgsrv:Build:AcceptNamespaceOnlyWithoutResourceLimits=true",
                "--Pkgsrv:Build:WorkspaceRoot=" + buildRoot,
                "--Pkgsrv:Build:WorkspaceQuotaBytes=268435456",
                "--Pkgsrv:Build:WorkspaceQuotaInodes=1000000",
                "--Pkgsrv:Build:Profiles:0:ToolchainName=gcc",
                "--Pkgsrv:Build:Profiles:0:ToolchainVersion=13.2.0",
                "--Pkgsrv:Build:Profiles:0:Executable=/usr/bin/make",
                "--Pkgsrv:Build:Profiles:0:OutputFile=out/package.bin"
            ]));
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public async Task InMemoryArtifacts_RemainAvailableWhenBuildsDisabled()
    {
        await using var app = PkgsrvHost.Create([]);

        Assert.IsType<InMemoryArtifactStore>(
            app.Services.GetRequiredService<IArtifactStore>());
    }

    [Fact]
    public void BuildEnabledWithoutArtifactQuota_FailsBeforeHostStarts()
    {
        var root = CreateRoot(out var artifactRoot, out var buildRoot);

        try
        {
            Assert.Throws<InvalidOperationException>(() => PkgsrvHost.Create(
            [
                "--Pkgsrv:Artifacts:Root=" + artifactRoot,
                "--Pkgsrv:Build:Enabled=true",
                "--Pkgsrv:Build:AcceptNamespaceOnlyWithoutResourceLimits=true",
                "--Pkgsrv:Build:WorkspaceRoot=" + buildRoot,
                "--Pkgsrv:Build:WorkspaceQuotaBytes=268435456",
                "--Pkgsrv:Build:WorkspaceQuotaInodes=1000000",
                "--Pkgsrv:Sources:EgressProxy=http://127.0.0.1:3128",
                "--Pkgsrv:Build:Profiles:0:ToolchainName=gcc",
                "--Pkgsrv:Build:Profiles:0:ToolchainVersion=13.2.0",
                "--Pkgsrv:Build:Profiles:0:Executable=/usr/bin/make",
                "--Pkgsrv:Build:Profiles:0:OutputFile=out/package.bin"
            ]));
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public void BuildEnabledWithoutEgressProxy_FailsBeforeHostStarts()
    {
        var root = CreateRoot(out var artifactRoot, out var buildRoot);

        try
        {
            Assert.Throws<InvalidOperationException>(() => PkgsrvHost.Create(
            [
                "--Pkgsrv:Artifacts:Root=" + artifactRoot,
                "--Pkgsrv:Artifacts:QuotaBytes=1073741824",
                "--Pkgsrv:Artifacts:QuotaInodes=1000000",
                "--Pkgsrv:Build:Enabled=true",
                "--Pkgsrv:Build:AcceptNamespaceOnlyWithoutResourceLimits=true",
                "--Pkgsrv:Build:WorkspaceRoot=" + buildRoot,
                "--Pkgsrv:Build:WorkspaceQuotaBytes=268435456",
                "--Pkgsrv:Build:WorkspaceQuotaInodes=1000000",
                "--Pkgsrv:Build:Profiles:0:ToolchainName=gcc",
                "--Pkgsrv:Build:Profiles:0:ToolchainVersion=13.2.0",
                "--Pkgsrv:Build:Profiles:0:Executable=/usr/bin/make",
                "--Pkgsrv:Build:Profiles:0:OutputFile=out/package.bin"
            ],
            artifactRootValidationOverride: (_, _, _, _) => true));
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public void BuildEnabledWithNonMountArtifactRoot_FailsBeforeHostStarts()
    {
        var root = CreateRoot(out var artifactRoot, out var buildRoot);

        try
        {
            Assert.Throws<InvalidOperationException>(() => PkgsrvHost.Create(
            [
                "--Pkgsrv:Artifacts:Root=" + artifactRoot,
                "--Pkgsrv:Artifacts:QuotaBytes=1073741824",
                "--Pkgsrv:Artifacts:QuotaInodes=1000000",
                "--Pkgsrv:Build:Enabled=true",
                "--Pkgsrv:Build:AcceptNamespaceOnlyWithoutResourceLimits=true",
                "--Pkgsrv:Build:WorkspaceRoot=" + buildRoot,
                "--Pkgsrv:Build:WorkspaceQuotaBytes=268435456",
                "--Pkgsrv:Build:WorkspaceQuotaInodes=1000000",
                "--Pkgsrv:Sources:EgressProxy=http://127.0.0.1:3128",
                "--Pkgsrv:Build:Profiles:0:ToolchainName=gcc",
                "--Pkgsrv:Build:Profiles:0:ToolchainVersion=13.2.0",
                "--Pkgsrv:Build:Profiles:0:Executable=/usr/bin/make",
                "--Pkgsrv:Build:Profiles:0:OutputFile=out/package.bin"
            ]));
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    private static string CreateRoot(out string artifactRoot, out string buildRoot)
    {
        var root = Path.Combine(Path.GetTempPath(), $"pkgsrv-admission-{Guid.NewGuid():N}");
        artifactRoot = Path.Combine(root, "artifacts");
        buildRoot = Path.Combine(root, "builds");
        Directory.CreateDirectory(root);
        Directory.CreateDirectory(artifactRoot);
        Directory.CreateDirectory(buildRoot);
        File.SetUnixFileMode(
            artifactRoot,
            UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
        File.SetUnixFileMode(
            buildRoot,
            UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
        return root;
    }

    [Fact]
    public async Task Host_SelectsBubblewrapAndVerifyingSigningWhenConfigured()
    {
        var root = Path.Combine(Path.GetTempPath(), $"pkgsrv-capabilities-{Guid.NewGuid():N}");
        var artifactRoot = Path.Combine(root, "artifacts");
        var buildRoot = Path.Combine(root, "builds");
        Directory.CreateDirectory(root);
        Directory.CreateDirectory(artifactRoot);
        Directory.CreateDirectory(buildRoot);
        File.SetUnixFileMode(
            artifactRoot,
            UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
        File.SetUnixFileMode(
            buildRoot,
            UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
        using var rsa = RSA.Create(2048);
        var privateKeyPath = Path.Combine(root, "private.pem");
        var publicKeyPath = Path.Combine(root, "public.pem");
        await File.WriteAllTextAsync(privateKeyPath, rsa.ExportPkcs8PrivateKeyPem());
        await File.WriteAllTextAsync(publicKeyPath, rsa.ExportSubjectPublicKeyInfoPem());
        File.SetUnixFileMode(
            privateKeyPath,
            UnixFileMode.UserRead | UnixFileMode.UserWrite);

        try
        {
            await using var app = PkgsrvHost.Create(
            [
                "--Pkgsrv:Artifacts:Root=" + artifactRoot,
                "--Pkgsrv:Artifacts:QuotaBytes=1073741824",
                "--Pkgsrv:Artifacts:QuotaInodes=1000000",
                "--Pkgsrv:Signing:PrivateKeyPath=" + privateKeyPath,
                "--Pkgsrv:Signing:PublicKeyPath=" + publicKeyPath,
                "--Pkgsrv:Build:Enabled=true",
                "--Pkgsrv:Build:AcceptNamespaceOnlyWithoutResourceLimits=true",
                "--Pkgsrv:Build:WorkspaceRoot=" + buildRoot,
                "--Pkgsrv:Build:WorkspaceQuotaBytes=268435456",
                "--Pkgsrv:Build:WorkspaceQuotaInodes=1000000",
                "--Pkgsrv:Sources:EgressProxy=http://127.0.0.1:3128",
                "--Pkgsrv:Build:Profiles:0:ToolchainName=gcc",
                "--Pkgsrv:Build:Profiles:0:ToolchainVersion=13.2.0",
                "--Pkgsrv:Build:Profiles:0:Executable=/usr/bin/make",
                "--Pkgsrv:Build:Profiles:0:OutputFile=out/package.bin"
            ],
            artifactRootValidationOverride: (_, _, _, _) => true);

            Assert.IsType<FileSystemArtifactStore>(
                app.Services.GetRequiredService<IArtifactStore>());
            Assert.IsType<RsaPackageSigner>(
                app.Services.GetRequiredService<IPackageSigner>());
            Assert.IsType<RsaPackageSignatureVerifier>(
                app.Services.GetRequiredService<IPackageSignatureVerifier>());
            Assert.IsType<VerifyingPackagePublisher>(
                app.Services.GetRequiredService<IPackagePublisher>());
            Assert.IsType<BubblewrapBuildSandbox>(
                app.Services.GetRequiredService<IBuildSandbox>());
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }
}
