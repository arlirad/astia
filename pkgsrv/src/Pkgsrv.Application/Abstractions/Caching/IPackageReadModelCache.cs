using Pkgsrv.Application.Contracts;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Abstractions.Caching;

public interface IPackageReadModelCache
{
    long CaptureGeneration();

    PackageDetailsDto? GetDetails(PackageKey key);

    void SetDetails(PackageDetailsDto package, long generation);

    IReadOnlyList<PackageSummaryDto>? GetList(string? search);

    void SetList(string? search, IReadOnlyList<PackageSummaryDto> packages, long generation);

    void InvalidatePackage(PackageKey key);

    void InvalidateLists();
}
