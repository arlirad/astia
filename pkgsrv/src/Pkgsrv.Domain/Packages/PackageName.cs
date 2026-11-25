using System.Text.RegularExpressions;
using Pkgsrv.Domain.Errors;

namespace Pkgsrv.Domain.Packages;

public sealed record PackageName
{
    private static readonly Regex Pattern = new(
        "^[a-z][a-z0-9]*(?:[._-][a-z0-9]+)*$",
        RegexOptions.CultureInvariant,
        TimeSpan.FromSeconds(1));

    private PackageName(string value)
    {
        Value = value;
    }

    public string Value { get; }

    public static PackageName Create(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new DomainException(DomainErrorCodes.PackageNameRequired, "Package name is required.");
        }

        var normalized = value.Trim();

        if (normalized.Length > 128 || !Pattern.IsMatch(normalized))
        {
            throw new DomainException(
                DomainErrorCodes.PackageNameInvalid,
                "Package name must be lowercase ASCII, start with a letter, and contain only letters, digits, dots, underscores, or hyphens.");
        }

        return new PackageName(normalized);
    }

    public override string ToString() => Value;
}
