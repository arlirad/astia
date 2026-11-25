using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public static class PostgresPackageMapper
{
    public static PackageRecord FromDomain(Package package)
    {
        ArgumentNullException.ThrowIfNull(package);

        var record = new PackageRecord();
        Apply(package, record);
        return record;
    }

    public static void Apply(Package package, PackageRecord record)
    {
        ArgumentNullException.ThrowIfNull(package);
        ArgumentNullException.ThrowIfNull(record);

        record.Name = package.Manifest.Name.Value;
        record.Version = package.Manifest.Version.ToString();
        record.SourceUrl = package.Manifest.Source.RepositoryUri.ToString();
        record.SourceCommit = package.Manifest.Source.CommitSha;
        record.ToolchainName = package.Manifest.Toolchain.Name;
        record.ToolchainVersion = package.Manifest.Toolchain.Version;
        record.ContentHash = package.Manifest.ContentHash.Value;
        record.Provenance = package.Manifest.Provenance.Value;
        record.SignatureStatus = (int)package.Manifest.Signature.Status;
        record.SignatureAlgorithm = package.Manifest.Signature.Algorithm;
        record.SignatureValue = package.Manifest.Signature.Value;
        record.BuildIsolation = (int)package.BuildPolicy.Isolation;
        record.AllowNetwork = package.BuildPolicy.AllowNetwork;
        record.TimeoutSeconds = (int)package.BuildPolicy.Timeout.TotalSeconds;
        record.State = (int)package.State;
        record.PendingEventsJson = PostgresPendingEventSerializer.Serialize(package.PeekDomainEvents());
        record.RegisteredAt = package.RegisteredAt;
        record.BuildRequestedAt = package.BuildRequestedAt;
    }

    public static PackageManifest ToManifest(PackageRecord record)
    {
        ArgumentNullException.ThrowIfNull(record);

        return PackageManifest.Create(
            PackageName.Create(record.Name),
            PackageVersion.Create(record.Version),
            PackageSource.Create(record.SourceUrl, record.SourceCommit),
            Toolchain.Create(record.ToolchainName, record.ToolchainVersion),
            ContentHash.Create(record.ContentHash),
            PackageProvenance.Create(record.Provenance),
            ToSignature(record));
    }

    public static Package ToDomain(PackageRecord record)
    {
        ArgumentNullException.ThrowIfNull(record);

        var manifest = ToManifest(record);
        var buildPolicy = BuildPolicy.Create(
            (BuildIsolation)record.BuildIsolation,
            record.AllowNetwork,
            TimeSpan.FromSeconds(record.TimeoutSeconds));
        var pendingEvents = PostgresPendingEventSerializer.Deserialize(
            record.PendingEventsJson,
            manifest,
            buildPolicy);

        return Package.Rehydrate(
            manifest,
            buildPolicy,
            ToState(record.State),
            record.RegisteredAt,
            record.BuildRequestedAt,
            pendingEvents);
    }

    public static PackageState ToState(int state)
    {
        var packageState = (PackageState)state;

        if (!Enum.IsDefined(packageState))
        {
            throw new InvalidOperationException($"Persisted package state '{state}' is invalid.");
        }

        return packageState;
    }

    private static PackageSignature ToSignature(PackageRecord record) =>
        (PackageSignatureStatus)record.SignatureStatus switch
        {
            PackageSignatureStatus.Unsigned => PackageSignature.Unsigned,
            PackageSignatureStatus.Pending => PackageSignature.Pending,
            PackageSignatureStatus.Signed => PackageSignature.Signed(
                record.SignatureAlgorithm,
                record.SignatureValue),
            _ => throw new InvalidOperationException(
                $"Persisted package signature status '{record.SignatureStatus}' is invalid.")
        };
}
