using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Packages;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PackageVersionOrderTests
{
    [Fact]
    public void Compare_OrdersNumericPreReleaseIdentifiersNumerically()
    {
        AssertLess("1.0.0-rc.2", "1.0.0-rc.10");
        AssertLess("1.0.0-2", "1.0.0-10");
        AssertLess(
            "1.0.0-999999999999999999999999",
            "1.0.0-1000000000000000000000000");
    }

    [Fact]
    public void Compare_OrdersNumericIdentifiersBelowAlphaNumericIdentifiers()
    {
        AssertLess("1.0.0-1", "1.0.0-0a");
        AssertLess("1.0.0-999999", "1.0.0-alpha");
        AssertLess("1.0.0-1.2.3", "1.0.0-alpha");
    }

    [Fact]
    public void Compare_OrdersShorterIdentifierSetsBelowLongerPrefixes()
    {
        AssertLess("1.0.0-alpha", "1.0.0-alpha.1");
        AssertLess("1.0.0-alpha.1", "1.0.0-alpha.1.1");
        AssertLess("1.0.0-1", "1.0.0-1.1");
    }

    [Fact]
    public void Compare_OrdersReleasesAbovePreReleases()
    {
        AssertLess("1.0.0-alpha", "1.0.0");
        AssertLess("1.0.0-rc.10", "1.0.0");
        AssertLess("1.0.0-alpha+zzz", "1.0.0");
    }

    [Fact]
    public void Compare_KeepsBuildMetadataOutOfPrecedenceWithDeterministicTieBreak()
    {
        AssertLess("1.0.0-rc.1+aaa", "1.0.0-rc.1+zzz");
        AssertLess("1.0.0-rc.1+build.99", "1.0.0-rc.2+build.1");
        AssertLess("1.0.0+build.10", "1.0.0+build.2");
    }

    [Fact]
    public void Compare_OrdersSemVerSpecificationExample()
    {
        string[] versions =
        [
            "1.0.0",
            "1.0.0-rc.1",
            "1.0.0-beta.11",
            "1.0.0-beta.2",
            "1.0.0-beta",
            "1.0.0-alpha.beta",
            "1.0.0-alpha.1",
            "1.0.0-alpha"
        ];

        var ordered = versions
            .Select(PackageVersion.Create)
            .OrderBy(version => version, PackageVersionOrder.Comparer)
            .Select(version => version.ToString());

        Assert.Equal(
            [
                "1.0.0-alpha",
                "1.0.0-alpha.1",
                "1.0.0-alpha.beta",
                "1.0.0-beta",
                "1.0.0-beta.2",
                "1.0.0-beta.11",
                "1.0.0-rc.1",
                "1.0.0"
            ],
            ordered);
    }

    [Fact]
    public void SortKey_OrdersLikeComparer()
    {
        string[] versions =
        [
            "1.0.0",
            "1.0.0-rc.10",
            "1.0.0-rc.2",
            "1.0.0-beta.11",
            "1.0.0-beta.2",
            "1.0.0-alpha.beta",
            "1.0.0-alpha.1",
            "1.0.0-alpha",
            "1.0.0-1",
            "1.0.0-01",
            "1.0.0-2",
            "1.0.0+build.2",
            "1.0.0+build.10"
        ];

        var byComparer = versions
            .Select(PackageVersion.Create)
            .OrderBy(version => version, PackageVersionOrder.Comparer)
            .Select(version => version.ToString());

        var bySortKey = versions
            .OrderBy(
                version => PackageVersionOrder.CreatePreReleaseSortKey(
                    PackageVersion.Create(version).PreRelease),
                StringComparer.Ordinal)
            .ThenBy(version => version, StringComparer.Ordinal);

        Assert.Equal(byComparer, bySortKey);
    }

    [Fact]
    public void Compare_TreatsNonSemVerNumericIdentifiersDeterministically()
    {
        AssertLess("1.0.0-1", "1.0.0-01");
        Assert.Equal(
            0,
            PackageVersionOrder.Compare(
                PackageVersion.Create("1.0.0-01"),
                PackageVersion.Create("1.0.0-01")));
    }

    private static void AssertLess(string left, string right)
    {
        var leftVersion = PackageVersion.Create(left);
        var rightVersion = PackageVersion.Create(right);

        Assert.True(
            PackageVersionOrder.Compare(leftVersion, rightVersion) < 0,
            $"Expected {left} to sort before {right}.");
        Assert.True(
            PackageVersionOrder.Compare(rightVersion, leftVersion) > 0,
            $"Expected {right} to sort after {left}.");
    }
}
