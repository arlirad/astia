using System.Text.RegularExpressions;
using Pkgsrv.Domain.Errors;

namespace Pkgsrv.Domain.Packages;

public sealed record Toolchain
{
    private static readonly Regex Pattern = new(
        "^[A-Za-z0-9][A-Za-z0-9._+-]{0,63}$",
        RegexOptions.CultureInvariant,
        TimeSpan.FromSeconds(1));

    private Toolchain(string name, string version)
    {
        Name = name;
        Version = version;
    }

    public string Name { get; }

    public string Version { get; }

    public static Toolchain Create(string? name, string? version)
    {
        if (string.IsNullOrWhiteSpace(name)
            || string.IsNullOrWhiteSpace(version)
            || !Pattern.IsMatch(name.Trim())
            || !Pattern.IsMatch(version.Trim()))
        {
            throw new DomainException(
                DomainErrorCodes.ToolchainInvalid,
                "Toolchain name and version are required and may contain only letters, digits, dots, underscores, plus signs, and hyphens.");
        }

        return new Toolchain(name.Trim(), version.Trim());
    }
}
