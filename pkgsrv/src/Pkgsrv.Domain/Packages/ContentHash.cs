using System.Text.RegularExpressions;
using Pkgsrv.Domain.Errors;

namespace Pkgsrv.Domain.Packages;

public sealed record ContentHash
{
    private static readonly Regex Sha256Pattern = new(
        "^[0-9a-fA-F]{64}$",
        RegexOptions.CultureInvariant,
        TimeSpan.FromSeconds(1));

    private ContentHash(string value)
    {
        Value = value;
    }

    public string Value { get; }

    public static ContentHash Create(string? value)
    {
        if (string.IsNullOrWhiteSpace(value) || !Sha256Pattern.IsMatch(value.Trim()))
        {
            throw new DomainException(
                DomainErrorCodes.ContentHashInvalid,
                "Content hash must be a 64-character hexadecimal SHA-256 value.");
        }

        return new ContentHash(value.Trim().ToLowerInvariant());
    }

    public override string ToString() => Value;
}
