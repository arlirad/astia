using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Interfaces.Caching;

public enum PackageCacheInvalidationScope
{
    All = 0,
    List = 1,
    Package = 2
}

public sealed record PackageCacheInvalidation
{
    private PackageCacheInvalidation(
        PackageCacheInvalidationScope scope,
        PackageKey? package)
    {
        Scope = scope;
        Package = package;
    }

    public PackageCacheInvalidationScope Scope { get; }

    public PackageKey? Package { get; }

    public static PackageCacheInvalidation All { get; } =
        new(PackageCacheInvalidationScope.All, null);

    public static PackageCacheInvalidation List { get; } =
        new(PackageCacheInvalidationScope.List, null);

    public static PackageCacheInvalidation ForPackage(PackageKey package)
    {
        ArgumentNullException.ThrowIfNull(package);

        return new(PackageCacheInvalidationScope.Package, package);
    }
}
