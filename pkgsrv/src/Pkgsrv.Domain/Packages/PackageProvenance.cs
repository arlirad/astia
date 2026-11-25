using Pkgsrv.Domain.Errors;

namespace Pkgsrv.Domain.Packages;

public sealed record PackageProvenance
{
    private PackageProvenance(string value)
    {
        Value = value;
    }

    public string Value { get; }

    public static PackageProvenance Create(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new DomainException(DomainErrorCodes.ProvenanceInvalid, "Provenance metadata is required.");
        }

        var normalized = value.Trim();

        if (normalized.Length > 1024)
        {
            throw new DomainException(
                DomainErrorCodes.ProvenanceInvalid,
                "Provenance metadata must not exceed 1024 characters.");
        }

        return new PackageProvenance(normalized);
    }

    public override string ToString() => Value;
}
