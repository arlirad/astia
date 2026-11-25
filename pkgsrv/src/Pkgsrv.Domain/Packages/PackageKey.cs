namespace Pkgsrv.Domain.Packages;

public sealed record PackageKey
{
    private PackageKey(PackageName name, PackageVersion version)
    {
        Name = name;
        Version = version;
    }

    public PackageName Name { get; }

    public PackageVersion Version { get; }

    public static PackageKey From(PackageName name, PackageVersion version)
    {
        ArgumentNullException.ThrowIfNull(name);
        ArgumentNullException.ThrowIfNull(version);

        return new PackageKey(name, version);
    }

    public static PackageKey Create(string? name, string? version) =>
        new(PackageName.Create(name), PackageVersion.Create(version));

    public override string ToString() => $"{Name.Value}@{Version}";
}
