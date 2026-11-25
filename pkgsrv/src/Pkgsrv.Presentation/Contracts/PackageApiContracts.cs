using Pkgsrv.Application.Contracts;

namespace Pkgsrv.Presentation.Contracts;

public sealed record RegisterPackageRequest(
    string? Name,
    string? Version,
    string? SourceUrl,
    string? SourceCommit,
    string? ToolchainName,
    string? ToolchainVersion,
    string? ContentHash,
    string? Provenance,
    string? Signature,
    string? SignatureAlgorithm,
    string? Isolation,
    bool AllowNetwork,
    int TimeoutSeconds);

public sealed record PackageSummaryResponse(
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
    public static PackageSummaryResponse From(PackageSummaryDto package) => new(
        package.Name,
        package.Version,
        package.SourceCommit,
        package.Toolchain,
        package.ContentHash,
        package.Provenance,
        package.SignatureStatus,
        package.State,
        package.RegisteredAt);
}

public sealed record PackageListResponse(IReadOnlyList<PackageSummaryResponse> Packages);

public sealed record PackageResponse(
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
    public static PackageResponse From(PackageDetailsDto package) => new(
        package.Name,
        package.Version,
        package.SourceUrl,
        package.SourceCommit,
        package.ToolchainName,
        package.ToolchainVersion,
        package.ContentHash,
        package.Provenance,
        package.SignatureStatus,
        package.SignatureAlgorithm,
        package.SignatureValue,
        package.Isolation,
        package.AllowNetwork,
        package.TimeoutSeconds,
        package.State,
        package.RegisteredAt,
        package.BuildRequestedAt);
}

public sealed record BuildResponse(
    string Name,
    string Version,
    string Status,
    string Detail,
    bool Executed)
{
    public static BuildResponse From(RequestBuildResult result) => new(
        result.Name,
        result.Version,
        result.Status,
        result.Detail,
        result.Executed);
}
