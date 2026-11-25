using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Packages;

public static class PackageVersionOrder
{
    public static IComparer<PackageVersion> Comparer { get; } =
        Comparer<PackageVersion>.Create(Compare);

    public static int Compare(PackageVersion? left, PackageVersion? right)
    {
        if (ReferenceEquals(left, right))
        {
            return 0;
        }

        if (left is null)
        {
            return -1;
        }

        if (right is null)
        {
            return 1;
        }

        var result = left.Major.CompareTo(right.Major);

        if (result != 0)
        {
            return result;
        }

        result = left.Minor.CompareTo(right.Minor);

        if (result != 0)
        {
            return result;
        }

        result = left.Patch.CompareTo(right.Patch);

        if (result != 0)
        {
            return result;
        }

        result = ComparePreRelease(left.PreRelease, right.PreRelease);

        if (result != 0)
        {
            return result;
        }

        result = CompareBuildMetadata(left.BuildMetadata, right.BuildMetadata);

        return result != 0
            ? result
            : StringComparer.Ordinal.Compare(left.ToString(), right.ToString());
    }

    private static int ComparePreRelease(string? left, string? right)
    {
        if (left is null && right is null)
        {
            return 0;
        }

        if (left is null)
        {
            return 1;
        }

        if (right is null)
        {
            return -1;
        }

        return StringComparer.Ordinal.Compare(left, right);
    }

    private static int CompareBuildMetadata(string? left, string? right)
    {
        if (left is null && right is null)
        {
            return 0;
        }

        if (left is null)
        {
            return -1;
        }

        if (right is null)
        {
            return 1;
        }

        return StringComparer.Ordinal.Compare(left, right);
    }
}
