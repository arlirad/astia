namespace Pkgsrv.Domain.Errors;

public static class DomainErrorCodes
{
    public const string PackageNameRequired = "package.name.required";
    public const string PackageNameInvalid = "package.name.invalid";
    public const string PackageVersionInvalid = "package.version.invalid";
    public const string SourceUrlInvalid = "package.source.url.invalid";
    public const string SourceCommitInvalid = "package.source.commit.invalid";
    public const string ToolchainInvalid = "package.toolchain.invalid";
    public const string ContentHashInvalid = "package.hash.invalid";
    public const string ProvenanceInvalid = "package.provenance.invalid";
    public const string SignatureInvalid = "package.signature.invalid";
    public const string SignaturePending = "package.signature.pending";
    public const string BuildPolicyInvalid = "package.build.policy.invalid";
    public const string BuildAlreadyRequested = "package.build.already-requested";
    public const string PackageStateInvalid = "package.state.invalid";
    public const string PackageStateConflict = "package.state.conflict";
}
