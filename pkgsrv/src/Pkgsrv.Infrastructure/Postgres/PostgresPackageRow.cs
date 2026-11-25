using Npgsql;
using NpgsqlTypes;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Postgres;

public sealed record PostgresPackageRow(
    string Name,
    string Version,
    int VersionMajor,
    int VersionMinor,
    int VersionPatch,
    string? VersionPreRelease,
    string? VersionBuildMetadata,
    string SourceUrl,
    string SourceCommit,
    string ToolchainName,
    string ToolchainVersion,
    string ContentHash,
    string Provenance,
    short SignatureStatus,
    string? SignatureAlgorithm,
    string? SignatureValue,
    short Isolation,
    bool AllowNetwork,
    long TimeoutTicks,
    short State,
    DateTimeOffset RegisteredAt,
    DateTimeOffset? BuildRequestedAt)
{
    public static PostgresPackageRow FromPackage(Package package)
    {
        ArgumentNullException.ThrowIfNull(package);

        var manifest = package.Manifest;
        var version = manifest.Version;
        var signature = manifest.Signature;

        return new PostgresPackageRow(
            manifest.Name.Value,
            version.ToString(),
            version.Major,
            version.Minor,
            version.Patch,
            version.PreRelease,
            version.BuildMetadata,
            manifest.Source.RepositoryUri.ToString(),
            manifest.Source.CommitSha,
            manifest.Toolchain.Name,
            manifest.Toolchain.Version,
            manifest.ContentHash.Value,
            manifest.Provenance.Value,
            (short)signature.Status,
            signature.Algorithm,
            signature.Value,
            (short)package.BuildPolicy.Isolation,
            package.BuildPolicy.AllowNetwork,
            package.BuildPolicy.Timeout.Ticks,
            (short)package.State,
            package.RegisteredAt.ToUniversalTime(),
            package.BuildRequestedAt?.ToUniversalTime());
    }

    public Package ToPackage()
    {
        var name = PackageName.Create(Name);
        var version = PackageVersion.Create(Version);

        if (version.Major != VersionMajor
            || version.Minor != VersionMinor
            || version.Patch != VersionPatch
            || version.PreRelease != VersionPreRelease
            || version.BuildMetadata != VersionBuildMetadata)
        {
            throw new InvalidOperationException(
                "Persisted package version columns do not match the version string.");
        }

        var manifest = PackageManifest.Create(
            name,
            version,
            PackageSource.Create(SourceUrl, SourceCommit),
            Toolchain.Create(ToolchainName, ToolchainVersion),
            Pkgsrv.Domain.Packages.ContentHash.Create(ContentHash),
            PackageProvenance.Create(Provenance),
            MapSignature());
        var buildPolicy = BuildPolicy.Create(
            (BuildIsolation)Isolation,
            AllowNetwork,
            TimeSpan.FromTicks(TimeoutTicks));

        return Package.Rehydrate(
            PackageKey.From(name, version),
            manifest,
            buildPolicy,
            RegisteredAt,
            (PackageState)State,
            BuildRequestedAt);
    }

    public void AddIdentityParameters(NpgsqlCommand command)
    {
        ArgumentNullException.ThrowIfNull(command);
        command.Parameters.Add(new NpgsqlParameter("name", NpgsqlDbType.Text) { Value = Name });
        command.Parameters.Add(new NpgsqlParameter("version", NpgsqlDbType.Text) { Value = Version });
    }

    public void AddInsertParameters(NpgsqlCommand command)
    {
        ArgumentNullException.ThrowIfNull(command);

        AddIdentityParameters(command);
        command.Parameters.Add(new NpgsqlParameter("version_major", NpgsqlDbType.Integer) { Value = VersionMajor });
        command.Parameters.Add(new NpgsqlParameter("version_minor", NpgsqlDbType.Integer) { Value = VersionMinor });
        command.Parameters.Add(new NpgsqlParameter("version_patch", NpgsqlDbType.Integer) { Value = VersionPatch });
        AddNullableText(command, "version_pre_release", VersionPreRelease);
        AddNullableText(command, "version_build_metadata", VersionBuildMetadata);
        command.Parameters.Add(new NpgsqlParameter("source_url", NpgsqlDbType.Text) { Value = SourceUrl });
        command.Parameters.Add(new NpgsqlParameter("source_commit", NpgsqlDbType.Text) { Value = SourceCommit });
        command.Parameters.Add(new NpgsqlParameter("toolchain_name", NpgsqlDbType.Text) { Value = ToolchainName });
        command.Parameters.Add(new NpgsqlParameter("toolchain_version", NpgsqlDbType.Text) { Value = ToolchainVersion });
        command.Parameters.Add(new NpgsqlParameter("content_hash", NpgsqlDbType.Text) { Value = ContentHash });
        command.Parameters.Add(new NpgsqlParameter("provenance", NpgsqlDbType.Text) { Value = Provenance });
        command.Parameters.Add(new NpgsqlParameter("signature_status", NpgsqlDbType.Smallint) { Value = SignatureStatus });
        AddNullableText(command, "signature_algorithm", SignatureAlgorithm);
        AddNullableText(command, "signature_value", SignatureValue);
        command.Parameters.Add(new NpgsqlParameter("isolation", NpgsqlDbType.Smallint) { Value = Isolation });
        command.Parameters.Add(new NpgsqlParameter("allow_network", NpgsqlDbType.Boolean) { Value = AllowNetwork });
        command.Parameters.Add(new NpgsqlParameter("timeout_ticks", NpgsqlDbType.Bigint) { Value = TimeoutTicks });
        AddStateParameters(command);
        command.Parameters.Add(new NpgsqlParameter("registered_at", NpgsqlDbType.TimestampTz) { Value = RegisteredAt });
    }

    public void AddStateParameters(NpgsqlCommand command)
    {
        ArgumentNullException.ThrowIfNull(command);

        command.Parameters.Add(new NpgsqlParameter("state", NpgsqlDbType.Smallint) { Value = State });
        command.Parameters.Add(
            new NpgsqlParameter("build_requested_at", NpgsqlDbType.TimestampTz)
            {
                Value = (object?)BuildRequestedAt ?? DBNull.Value
            });
    }

    public static PostgresPackageRow Read(NpgsqlDataReader reader)
    {
        ArgumentNullException.ThrowIfNull(reader);

        return new PostgresPackageRow(
            reader.GetString(reader.GetOrdinal("name")),
            reader.GetString(reader.GetOrdinal("version")),
            reader.GetInt32(reader.GetOrdinal("version_major")),
            reader.GetInt32(reader.GetOrdinal("version_minor")),
            reader.GetInt32(reader.GetOrdinal("version_patch")),
            ReadNullableText(reader, "version_pre_release"),
            ReadNullableText(reader, "version_build_metadata"),
            reader.GetString(reader.GetOrdinal("source_url")),
            reader.GetString(reader.GetOrdinal("source_commit")),
            reader.GetString(reader.GetOrdinal("toolchain_name")),
            reader.GetString(reader.GetOrdinal("toolchain_version")),
            reader.GetString(reader.GetOrdinal("content_hash")),
            reader.GetString(reader.GetOrdinal("provenance")),
            reader.GetInt16(reader.GetOrdinal("signature_status")),
            ReadNullableText(reader, "signature_algorithm"),
            ReadNullableText(reader, "signature_value"),
            reader.GetInt16(reader.GetOrdinal("isolation")),
            reader.GetBoolean(reader.GetOrdinal("allow_network")),
            reader.GetInt64(reader.GetOrdinal("timeout_ticks")),
            reader.GetInt16(reader.GetOrdinal("state")),
            reader.GetFieldValue<DateTimeOffset>(reader.GetOrdinal("registered_at")),
            ReadNullableTimestamp(reader, "build_requested_at"));
    }

    private PackageSignature MapSignature() => SignatureStatus switch
    {
        (short)PackageSignatureStatus.Unsigned => PackageSignature.Unsigned,
        (short)PackageSignatureStatus.Pending => PackageSignature.Pending,
        (short)PackageSignatureStatus.Signed => PackageSignature.Signed(SignatureAlgorithm, SignatureValue),
        _ => throw new InvalidOperationException("Persisted package signature status is invalid.")
    };

    private static void AddNullableText(NpgsqlCommand command, string name, string? value) =>
        command.Parameters.Add(
            new NpgsqlParameter(name, NpgsqlDbType.Text)
            {
                Value = (object?)value ?? DBNull.Value
            });

    private static string? ReadNullableText(NpgsqlDataReader reader, string column)
    {
        var ordinal = reader.GetOrdinal(column);
        return reader.IsDBNull(ordinal) ? null : reader.GetString(ordinal);
    }

    private static DateTimeOffset? ReadNullableTimestamp(NpgsqlDataReader reader, string column)
    {
        var ordinal = reader.GetOrdinal(column);
        return reader.IsDBNull(ordinal) ? null : reader.GetFieldValue<DateTimeOffset>(ordinal);
    }
}
