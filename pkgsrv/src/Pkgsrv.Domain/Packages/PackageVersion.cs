using System.Text.RegularExpressions;
using Pkgsrv.Domain.Errors;

namespace Pkgsrv.Domain.Packages;

public sealed record PackageVersion
{
    private static readonly Regex Pattern = new(
        "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z.-]+))?(?:\\+([0-9A-Za-z.-]+))?$",
        RegexOptions.CultureInvariant,
        TimeSpan.FromSeconds(1));

    private PackageVersion(int major, int minor, int patch, string? preRelease, string? buildMetadata)
    {
        Major = major;
        Minor = minor;
        Patch = patch;
        PreRelease = preRelease;
        BuildMetadata = buildMetadata;
    }

    public int Major { get; }

    public int Minor { get; }

    public int Patch { get; }

    public string? PreRelease { get; }

    public string? BuildMetadata { get; }

    public static PackageVersion Create(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new DomainException(DomainErrorCodes.PackageVersionInvalid, "Package version is required.");
        }

        var normalized = value.Trim();
        var match = Pattern.Match(normalized);

        if (!match.Success
            || !int.TryParse(match.Groups[1].Value, out var major)
            || !int.TryParse(match.Groups[2].Value, out var minor)
            || !int.TryParse(match.Groups[3].Value, out var patch))
        {
            throw new DomainException(
                DomainErrorCodes.PackageVersionInvalid,
                "Package version must be a semantic version such as 1.2.3 or 1.2.3-rc.1.");
        }

        var preRelease = match.Groups[4].Success ? match.Groups[4].Value : null;
        var buildMetadata = match.Groups[5].Success ? match.Groups[5].Value : null;

        return new PackageVersion(major, minor, patch, preRelease, buildMetadata);
    }

    public override string ToString()
    {
        var value = $"{Major}.{Minor}.{Patch}";

        if (PreRelease is not null)
        {
            value += $"-{PreRelease}";
        }

        if (BuildMetadata is not null)
        {
            value += $"+{BuildMetadata}";
        }

        return value;
    }
}
