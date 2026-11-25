using Microsoft.EntityFrameworkCore;
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
        Assert.Equal((int)package.BuildPolicy.Timeout.TotalSeconds, record.TimeoutSeconds);
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
}
