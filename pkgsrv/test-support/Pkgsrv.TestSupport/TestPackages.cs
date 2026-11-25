using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Features.Packages.Register;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.TestSupport;

public static class TestPackages
{
    public const string CommitSha = "0123456789abcdef0123456789abcdef01234567";
    public const string ContentHashValue = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    public const string SourceUrlValue = "https://github.com/example/widgets";

    public static readonly DateTimeOffset Timestamp =
        new(2026, 1, 2, 3, 4, 5, TimeSpan.Zero);

    public static AuthorizationContext Actor { get; } =
        AuthorizationContext.Create("test-actor", ["publisher", "builder", "reader"]);

    public static PackageManifest CreateManifest(
        PackageSignature? signature = null,
        string name = "widget",
        string version = "1.2.3",
        string? sourceUrl = null,
        string? sourceCommit = null,
        string contentHash = ContentHashValue) =>
        PackageManifest.Create(
            PackageName.Create(name),
            PackageVersion.Create(version),
            PackageSource.Create(sourceUrl ?? SourceUrlValue, sourceCommit ?? CommitSha),
            Toolchain.Create("gcc", "13.2.0"),
            ContentHash.Create(contentHash),
            PackageProvenance.Create("ci://pkgsrv/tests/42"),
            signature ?? PackageSignature.Pending);

    public static PackageSignature CreateSignedSignature() =>
        PackageSignature.Signed("ed25519", "c2lnbmF0dXJlLXZhbHVl");

    public static Package CreatePackage(
        bool signed = false,
        string name = "widget",
        string version = "1.2.3") =>
        Package.Register(
            CreateManifest(
                signed ? CreateSignedSignature() : PackageSignature.Pending,
                name,
                version),
            BuildPolicy.Create(BuildIsolation.Container, false, TimeSpan.FromMinutes(5)),
            Timestamp);

    public static RegisterPackageCommand CreateRegistrationCommand(
        AuthorizationContext? actor = null,
        string? name = "widget",
        string? version = "1.2.3",
        string? sourceUrl = SourceUrlValue,
        string? sourceCommit = CommitSha,
        string? signature = null,
        string? signatureAlgorithm = null,
        string? isolation = "Container",
        bool allowNetwork = false,
        int timeoutSeconds = 300) =>
        new(
            name,
            version,
            sourceUrl,
            sourceCommit,
            "gcc",
            "13.2.0",
            ContentHashValue,
            "ci://pkgsrv/tests/42",
            signature,
            signatureAlgorithm,
            isolation,
            allowNetwork,
            timeoutSeconds,
            actor ?? Actor);
}
