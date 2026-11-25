using Pkgsrv.Domain.Errors;

namespace Pkgsrv.Domain.Packages;

public enum PackageSignatureStatus
{
    Unsigned = 0,
    Pending = 1,
    Signed = 2
}

public sealed record PackageSignature
{
    private PackageSignature(PackageSignatureStatus status, string? algorithm, string? value)
    {
        Status = status;
        Algorithm = algorithm;
        Value = value;
    }

    public PackageSignatureStatus Status { get; }

    public string? Algorithm { get; }

    public string? Value { get; }

    public bool IsPublishable => Status == PackageSignatureStatus.Signed;

    public static PackageSignature Unsigned { get; } =
        new(PackageSignatureStatus.Unsigned, null, null);

    public static PackageSignature Pending { get; } =
        new(PackageSignatureStatus.Pending, null, null);

    public static PackageSignature Signed(string? algorithm, string? value)
    {
        if (string.IsNullOrWhiteSpace(algorithm)
            || string.IsNullOrWhiteSpace(value)
            || algorithm.Trim().Length > 32
            || value.Trim().Length > 4096)
        {
            throw new DomainException(
                DomainErrorCodes.SignatureInvalid,
                "A signed package requires a signature algorithm and value.");
        }

        return new PackageSignature(PackageSignatureStatus.Signed, algorithm.Trim(), value.Trim());
    }

    public void EnsurePublishable()
    {
        if (!IsPublishable)
        {
            throw new DomainException(
                DomainErrorCodes.SignaturePending,
                "Package publication requires a verified signature.");
        }
    }
}
