using Microsoft.EntityFrameworkCore;
using Pkgsrv.Domain.Errors;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Persistence.Postgres;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPackageMapperTests
{
    [Fact]
    public void RoundTrip_PreservesManifestPolicyAndRegisteredState()
    {
        var package = TestPackages.CreatePackage(signed: true);
        package.AcknowledgeDomainEvents(package.PeekDomainEvents());
        var record = PostgresPackageMapper.FromDomain(package);

        Assert.Equal(package.Key.Name.Value, record.Name);
        Assert.Equal(package.Key.Version.ToString(), record.Version);
        Assert.Equal(package.Manifest.Version.Major, record.VersionMajor);
        Assert.Equal(package.Manifest.Version.Minor, record.VersionMinor);
        Assert.Equal(package.Manifest.Version.Patch, record.VersionPatch);
        Assert.Equal(package.Manifest.Version.PreRelease, record.VersionPreRelease);
        Assert.Equal(package.Manifest.Version.BuildMetadata, record.VersionBuildMetadata);
        Assert.Equal(package.Manifest.Source.RepositoryUri.ToString(), record.SourceUrl);
        Assert.Equal(package.Manifest.Source.CommitSha, record.SourceCommit);
        Assert.Equal(package.Manifest.Toolchain.Name, record.ToolchainName);
        Assert.Equal(package.Manifest.Toolchain.Version, record.ToolchainVersion);
        Assert.Equal(package.Manifest.ContentHash.Value, record.ContentHash);
        Assert.Equal(package.Manifest.Provenance.Value, record.Provenance);
        Assert.Equal((int)PackageSignatureStatus.Signed, record.SignatureStatus);
        Assert.Equal(package.Manifest.Signature.Algorithm, record.SignatureAlgorithm);
        Assert.Equal(package.Manifest.Signature.Value, record.SignatureValue);
        Assert.Equal((int)BuildIsolation.Container, record.BuildIsolation);
        Assert.Equal(package.BuildPolicy.AllowNetwork, record.AllowNetwork);
        Assert.Equal(package.BuildPolicy.Timeout.Ticks, record.TimeoutTicks);
        Assert.Equal((int)PackageState.Registered, record.State);
        Assert.Equal(package.RegisteredAt, record.RegisteredAt);
        Assert.Null(record.BuildRequestedAt);

        var rehydrated = PostgresPackageMapper.ToDomain(record);

        Assert.Equal(package.Key, rehydrated.Key);
        Assert.Equal(package.Manifest.ContentHash, rehydrated.Manifest.ContentHash);
        Assert.Equal(package.Manifest.Signature, rehydrated.Manifest.Signature);
        Assert.Equal(package.BuildPolicy, rehydrated.BuildPolicy);
        Assert.Equal(PackageState.Registered, rehydrated.State);
        Assert.Equal(package.RegisteredAt, rehydrated.RegisteredAt);
        Assert.Empty(rehydrated.DomainEvents);
    }

    [Fact]
    public void RoundTrip_PreservesBuildRequestedStateWithoutEvents()
    {
        var package = TestPackages.CreatePackage();
        package.AcknowledgeDomainEvents(package.PeekDomainEvents());
        var requestedAt = TestPackages.Timestamp.AddMinutes(7);
        package.RequestBuild(requestedAt);

        var rehydrated = PostgresPackageMapper.ToDomain(
            PostgresPackageMapper.FromDomain(package));

        Assert.Equal(PackageState.BuildRequested, rehydrated.State);
        Assert.Equal(requestedAt, rehydrated.BuildRequestedAt);

        var pending = Assert.IsType<PackageBuildRequested>(
            Assert.Single(rehydrated.DomainEvents));
        Assert.Equal(requestedAt, pending.OccurredAt);
    }

    [Fact]
    public void RoundTrip_PreservesPendingRegisteredEventForRestart()
    {
        var package = TestPackages.CreatePackage();
        var record = PostgresPackageMapper.FromDomain(package);

        var rehydrated = PostgresPackageMapper.ToDomain(record);

        var pending = Assert.IsType<PackageRegistered>(
            Assert.Single(rehydrated.DomainEvents));
        Assert.Equal(package.Key, pending.Key);
        Assert.Equal(package.RegisteredAt, pending.OccurredAt);
    }

    [Fact]
    public void RoundTrip_PreservesPublishedStateAndTimestamp()
    {
        var package = TestPackages.CreatePackage(signed: true);
        package.AcknowledgeDomainEvents(package.PeekDomainEvents());
        package.RequestBuild(TestPackages.Timestamp.AddMinutes(1));
        package.AcknowledgeDomainEvents(package.PeekDomainEvents());
        var publishedAt = TestPackages.Timestamp.AddMinutes(2);
        package.MarkPublished(publishedAt);

        var record = PostgresPackageMapper.FromDomain(package);
        var rehydrated = PostgresPackageMapper.ToDomain(record);

        Assert.Equal((int)PackageState.Published, record.State);
        Assert.Equal(publishedAt, record.PublishedAt);
        Assert.Equal(PackageState.Published, rehydrated.State);
        Assert.Equal(publishedAt, rehydrated.PublishedAt);
        Assert.Empty(rehydrated.DomainEvents);
    }

    [Fact]
    public void ToDomain_RejectsPublishedStateWithoutTimestamp()
    {
        var record = PostgresPackageMapper.FromDomain(TestPackages.CreatePackage(signed: true));
        record.State = (int)PackageState.Published;
        record.BuildRequestedAt = TestPackages.Timestamp;
        record.PublishedAt = null;

        Assert.Throws<DomainException>(
            () => PostgresPackageMapper.ToDomain(record));
    }

    [Fact]
    public void ToDomain_RejectsPendingEventPayloadForUnsupportedKind()
    {
        var record = PostgresPackageMapper.FromDomain(TestPackages.CreatePackage());
        record.PendingEventsJson = """[{"type":"Unsupported","name":"widget","version":"1.2.3","occurredAt":"2026-01-02T03:04:05+00:00"}]""";

        Assert.Throws<InvalidOperationException>(
            () => PostgresPackageMapper.ToDomain(record));
    }

    [Fact]
    public void ToDomain_RejectsInvalidPersistedState()
    {
        var record = PostgresPackageMapper.FromDomain(TestPackages.CreatePackage());
        record.State = 99;

        Assert.Throws<InvalidOperationException>(
            () => PostgresPackageMapper.ToDomain(record));
    }

    [Fact]
    public void ToDomain_RejectsInvalidPersistedSignatureStatus()
    {
        var record = PostgresPackageMapper.FromDomain(TestPackages.CreatePackage());
        record.SignatureStatus = 99;

        Assert.Throws<InvalidOperationException>(
            () => PostgresPackageMapper.ToDomain(record));
    }

    [Fact]
    public void RoundTrip_PreservesFractionalBuildTimeout()
    {
        var package = Package.Register(
            TestPackages.CreateManifest(),
            BuildPolicy.Create(
                BuildIsolation.Container,
                allowNetwork: false,
                timeout: TimeSpan.FromMilliseconds(1500)),
            TestPackages.Timestamp);

        var rehydrated = PostgresPackageMapper.ToDomain(
            PostgresPackageMapper.FromDomain(package));

        Assert.Equal(TimeSpan.FromMilliseconds(1500), rehydrated.BuildPolicy.Timeout);
    }
}
