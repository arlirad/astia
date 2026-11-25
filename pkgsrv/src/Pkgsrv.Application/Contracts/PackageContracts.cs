using Pkgsrv.Application.Abstractions.Persistence;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Contracts;

public sealed record PackageSummaryDto(
    string Name,
    string Version,
    string SourceCommit,
    string Toolchain,
    string ContentHash,
    string Provenance,
    string SignatureStatus,
    string State,
    DateTimeOffset RegisteredAt)
{
    public static PackageSummaryDto From(PackageIndexEntry entry) => new(
        entry.Manifest.Name.Value,
        entry.Manifest.Version.ToString(),
        entry.Manifest.Source.CommitSha,
        $"{entry.Manifest.Toolchain.Name} {entry.Manifest.Toolchain.Version}",
        entry.Manifest.ContentHash.Value,
        entry.Manifest.Provenance.Value,
        entry.Manifest.Signature.Status.ToString(),
        entry.State.ToString(),
        entry.RegisteredAt);
}

public sealed record PackageDetailsDto(
    string Name,
    string Version,
    string SourceUrl,
    string SourceCommit,
    string ToolchainName,
    string ToolchainVersion,
    string ContentHash,
    string Provenance,
    string SignatureStatus,
    string? SignatureAlgorithm,
    string? SignatureValue,
    string Isolation,
    bool AllowNetwork,
    int TimeoutSeconds,
    string State,
    DateTimeOffset RegisteredAt,
    DateTimeOffset? BuildRequestedAt)
{
    public static PackageDetailsDto From(Package package) => new(
        package.Manifest.Name.Value,
        package.Manifest.Version.ToString(),
        package.Manifest.Source.RepositoryUri.ToString(),
        package.Manifest.Source.CommitSha,
        package.Manifest.Toolchain.Name,
        package.Manifest.Toolchain.Version,
        package.Manifest.ContentHash.Value,
        package.Manifest.Provenance.Value,
        package.Manifest.Signature.Status.ToString(),
        package.Manifest.Signature.Algorithm,
        package.Manifest.Signature.Value,
        package.BuildPolicy.Isolation.ToString(),
        package.BuildPolicy.AllowNetwork,
        (int)package.BuildPolicy.Timeout.TotalSeconds,
        package.State.ToString(),
        package.RegisteredAt,
        package.BuildRequestedAt);
}

public sealed record RegisterPackageResult(PackageDetailsDto Package);

public sealed record RequestBuildResult(
    string Name,
    string Version,
    string Status,
    string Detail,
    bool Executed);
