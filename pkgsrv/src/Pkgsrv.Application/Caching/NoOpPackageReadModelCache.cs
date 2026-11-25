using Pkgsrv.Application.Abstractions.Caching;
using Pkgsrv.Application.Contracts;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Caching;

public sealed class NoOpPackageReadModelCache : IPackageReadModelCache
{
    public static NoOpPackageReadModelCache Instance { get; } = new();

    private NoOpPackageReadModelCache()
    {
    }

    public long CaptureGeneration() => 0;

    public PackageDetailsDto? GetDetails(PackageKey key) => null;

    public void SetDetails(PackageDetailsDto package, long generation)
    {
    }

    public IReadOnlyList<PackageSummaryDto>? GetList(string? search) => null;

    public void SetList(string? search, IReadOnlyList<PackageSummaryDto> packages, long generation)
    {
    }

    public void InvalidatePackage(PackageKey key)
    {
    }

    public void InvalidateLists()
    {
    }
}
