using System.Runtime.Versioning;
using Pkgsrv.Infrastructure.Artifacts;
using Pkgsrv.Infrastructure.Builds;

namespace Pkgsrv.Infrastructure.Tests;

[SupportedOSPlatform("linux")]
public sealed class ArtifactRootValidatorTests
{
    private static LinuxMountEntry DedicatedArtifactMount(string root = "/") =>
        new(
            Id: 10,
            ParentId: 1,
            Device: "8:17",
            Root: root,
            MountPoint: "/var/lib/pkgsrv/artifacts",
            FileSystemType: "ext4",
            Source: "/dev/sdb1");

    private static LinuxMountEntry ParentMount(string device = "8:1") =>
        new(
            Id: 1,
            ParentId: 0,
            Device: device,
            Root: "/",
            MountPoint: "/",
            FileSystemType: "ext4",
            Source: "/dev/sda1");

    [Fact]
    public void DedicatedMountIdentity_AcceptsExactRootOnDistinctDevice()
    {
        Assert.True(WorkspaceFilesystemQuota.IsDedicatedMountIdentity(
            "/var/lib/pkgsrv/artifacts",
            DedicatedArtifactMount(),
            ParentMount()));
    }

    [Fact]
    public void BindMountedSubdirectory_IsRejected()
    {
        Assert.False(WorkspaceFilesystemQuota.IsDedicatedMountIdentity(
            "/var/lib/pkgsrv/artifacts",
            DedicatedArtifactMount(root: "/srv/artifacts-parent"),
            ParentMount()));
    }

    [Fact]
    public void SharedDeviceWithParent_IsRejected()
    {
        Assert.False(WorkspaceFilesystemQuota.IsDedicatedMountIdentity(
            "/var/lib/pkgsrv/artifacts",
            DedicatedArtifactMount(),
            ParentMount(device: "8:17")));
    }

    [Fact]
    public void MissingParentMount_IsRejected()
    {
        Assert.False(WorkspaceFilesystemQuota.IsDedicatedMountIdentity(
            "/var/lib/pkgsrv/artifacts",
            DedicatedArtifactMount(),
            parent: null));
    }

    [Fact]
    public void WorkspaceTmpfsMountIdentity_RequiresExactRootAndDistinctDevice()
    {
        var mount = new LinuxMountEntry(
            Id: 20,
            ParentId: 1,
            Device: "0:42",
            Root: "/",
            MountPoint: "/run/pkgsrv/builds",
            FileSystemType: "tmpfs",
            Source: "tmpfs");

        Assert.True(WorkspaceFilesystemQuota.IsDedicatedMountIdentity(
            "/run/pkgsrv/builds",
            mount,
            ParentMount()));
        Assert.False(WorkspaceFilesystemQuota.IsDedicatedMountIdentity(
            "/run/pkgsrv/builds",
            mount with { Root = "/subdirectory" },
            ParentMount()));
        Assert.False(WorkspaceFilesystemQuota.IsDedicatedMountIdentity(
            "/run/pkgsrv/builds",
            mount,
            ParentMount(device: "0:42")));
    }

    [Fact]
    public void MissingRoot_IsRejected()
    {
        var root = Path.Combine(Path.GetTempPath(), $"pkgsrv-artifact-missing-{Guid.NewGuid():N}");

        Assert.False(ArtifactRootValidator.TryValidate(
            root,
            1024,
            10,
            1024,
            out _));
    }

    [Fact]
    public void PlainDirectory_IsRejectedBecauseItIsNotAnExactMountpoint()
    {
        var root = Path.Combine(Path.GetTempPath(), $"pkgsrv-artifact-mount-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);
        File.SetUnixFileMode(
            root,
            UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);

        try
        {
            Assert.False(ArtifactRootValidator.TryValidate(
                root,
                1024,
                10,
                1024,
                out _));
        }
        finally
        {
            Directory.Delete(root);
        }
    }

    [Theory]
    [InlineData("tmpfs")]
    [InlineData("ramfs")]
    public void NonDurableFilesystem_IsRejected(string fileSystemType)
    {
        Assert.False(ArtifactRootValidator.MeetsCaps(
            fileSystemType,
            1024,
            10,
            2048,
            20));
    }

    [Fact]
    public void ByteOrInodeCapacityOverMaximum_IsRejected()
    {
        Assert.False(ArtifactRootValidator.MeetsCaps(
            "ext4",
            2049,
            10,
            2048,
            20));
        Assert.False(ArtifactRootValidator.MeetsCaps(
            "ext4",
            1024,
            21,
            2048,
            20));
        Assert.True(ArtifactRootValidator.MeetsCaps(
            "ext4",
            1024,
            10,
            2048,
            20));
    }

    [Fact]
    public void ArtifactCapacity_RequiresThreeAvailableInodes()
    {
        Assert.Equal(3, ArtifactRootValidator.RequiredAvailableInodes);
        Assert.False(ArtifactRootValidator.MeetsAvailableCapacity(
            availableBytes: 1024,
            availableInodes: 2,
            requiredBytes: 1024,
            requiredInodes: ArtifactRootValidator.RequiredAvailableInodes));
        Assert.True(ArtifactRootValidator.MeetsAvailableCapacity(
            availableBytes: 1024,
            availableInodes: 3,
            requiredBytes: 1024,
            requiredInodes: ArtifactRootValidator.RequiredAvailableInodes));
    }

    [Fact]
    public void InsufficientAvailableCapacity_IsRejected()
    {
        Assert.False(ArtifactRootValidator.MeetsAvailableCapacity(
            availableBytes: 1023,
            availableInodes: 10,
            requiredBytes: 1024,
            requiredInodes: 2));
        Assert.False(ArtifactRootValidator.MeetsAvailableCapacity(
            availableBytes: 1024,
            availableInodes: 1,
            requiredBytes: 1024,
            requiredInodes: 2));
        Assert.True(ArtifactRootValidator.MeetsAvailableCapacity(
            availableBytes: 1024,
            availableInodes: 2,
            requiredBytes: 1024,
            requiredInodes: 2));
    }
}
