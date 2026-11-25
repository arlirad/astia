using Npgsql;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Postgres;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPackageMappingTests
{
    [Fact]
    public void Row_RoundTripsRegisteredPackage()
    {
        var package = TestPackages.CreatePackage();

        var restored = PostgresPackageRow.FromPackage(package).ToPackage();

        Assert.Equal(package.Key, restored.Key);
        Assert.Equal(package.Manifest.Source.RepositoryUri, restored.Manifest.Source.RepositoryUri);
        Assert.Equal(package.Manifest.Source.CommitSha, restored.Manifest.Source.CommitSha);
        Assert.Equal(package.Manifest.Toolchain.Name, restored.Manifest.Toolchain.Name);
        Assert.Equal(package.Manifest.Toolchain.Version, restored.Manifest.Toolchain.Version);
        Assert.Equal(package.Manifest.ContentHash.Value, restored.Manifest.ContentHash.Value);
        Assert.Equal(package.Manifest.Provenance.Value, restored.Manifest.Provenance.Value);
        Assert.Equal(PackageSignatureStatus.Pending, restored.Manifest.Signature.Status);
        Assert.Equal(package.BuildPolicy.Isolation, restored.BuildPolicy.Isolation);
        Assert.Equal(package.BuildPolicy.AllowNetwork, restored.BuildPolicy.AllowNetwork);
        Assert.Equal(package.BuildPolicy.Timeout, restored.BuildPolicy.Timeout);
        Assert.Equal(PackageState.Registered, restored.State);
        Assert.Null(restored.BuildRequestedAt);
        Assert.Equal(package.RegisteredAt, restored.RegisteredAt);
        Assert.Empty(restored.DomainEvents);
    }

    [Fact]
    public void Row_RoundTripsBuildRequestedSignedPackage()
    {
        var package = TestPackages.CreatePackage(signed: true);
        package.RequestBuild(TestPackages.Timestamp.AddMinutes(5));

        var restored = PostgresPackageRow.FromPackage(package).ToPackage();

        Assert.Equal(PackageState.BuildRequested, restored.State);
        Assert.Equal(TestPackages.Timestamp.AddMinutes(5), restored.BuildRequestedAt);
        Assert.Equal(PackageSignatureStatus.Signed, restored.Manifest.Signature.Status);
        Assert.Equal("ed25519", restored.Manifest.Signature.Algorithm);
        Assert.Equal("c2lnbmF0dXJlLXZhbHVl", restored.Manifest.Signature.Value);
        Assert.Empty(restored.DomainEvents);
    }

    [Fact]
    public void Row_NormalizesTimestampsToUtc()
    {
        var localTimestamp = new DateTimeOffset(2026, 1, 2, 3, 4, 5, TimeSpan.FromHours(2));
        var package = Package.Register(
            TestPackages.CreateManifest(),
            BuildPolicy.Create(BuildIsolation.Container, false, TimeSpan.FromMinutes(5)),
            localTimestamp);

        var restored = PostgresPackageRow.FromPackage(package).ToPackage();

        Assert.Equal(TimeSpan.Zero, restored.RegisteredAt.Offset);
        Assert.Equal(localTimestamp, restored.RegisteredAt);
    }

    [Fact]
    public void Row_PreservesFractionalBuildTimeout()
    {
        var package = Package.Register(
            TestPackages.CreateManifest(),
            BuildPolicy.Create(
                BuildIsolation.Container,
                allowNetwork: false,
                timeout: TimeSpan.FromMilliseconds(1500)),
            TestPackages.Timestamp);

        var restored = PostgresPackageRow.FromPackage(package).ToPackage();

        Assert.Equal(TimeSpan.FromMilliseconds(1500), restored.BuildPolicy.Timeout);
    }
}
