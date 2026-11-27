using System.Text;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Packages;

public static class PackageVersionOrder
{
    public const string ReleaseSortKey = "\u0004";

    private const string IdentifierSeparator = "\u0001";
    private const string NumericIdentifierMarker = "\u0002";
    private const string AlphaNumericIdentifierMarker = "\u0003";

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

        result = string.CompareOrdinal(
            CreatePreReleaseSortKey(left.PreRelease),
            CreatePreReleaseSortKey(right.PreRelease));

        if (result != 0)
        {
            return result;
        }

        return StringComparer.Ordinal.Compare(left.ToString(), right.ToString());
    }

    public static string CreatePreReleaseSortKey(string? preRelease)
    {
        if (preRelease is null)
        {
            return ReleaseSortKey;
        }

        var identifiers = preRelease.Split('.');
        var builder = new StringBuilder(preRelease.Length + identifiers.Length);

        for (var index = 0; index < identifiers.Length; index++)
        {
            if (index > 0)
            {
                builder.Append(IdentifierSeparator);
            }

            AppendIdentifier(builder, identifiers[index]);
        }

        return builder.ToString();
    }

    private static void AppendIdentifier(StringBuilder builder, string identifier)
    {
        if (IsNumericIdentifier(identifier))
        {
            builder.Append(NumericIdentifierMarker);
            builder.Append(NumericIdentifierMarker[0], identifier.Length);
            builder.Append(IdentifierSeparator);
            builder.Append(identifier);
            return;
        }

        builder.Append(AlphaNumericIdentifierMarker);
        builder.Append(identifier);
    }

    private static bool IsNumericIdentifier(string identifier)
    {
        if (identifier.Length == 0
            || (identifier.Length > 1 && identifier[0] == '0'))
        {
            return false;
        }

        foreach (var character in identifier)
        {
            if (character is < '0' or > '9')
            {
                return false;
            }
        }

        return true;
    }
}
