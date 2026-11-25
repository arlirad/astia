using Pkgsrv.Domain.Errors;
using Pkgsrv.Domain.Packages;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Domain.Tests;

public sealed class PackageValueObjectTests
{
    [Fact]
    public void PackageName_AcceptsLowercaseNames()
    {
        Assert.Equal("widget", PackageName.Create("  widget  ").Value);
        Assert.Equal("libc.sys", PackageName.Create("libc.sys").Value);
        Assert.Equal("linux-utils", PackageName.Create("linux-utils").Value);
    }

    [Theory]
    [InlineData("", DomainErrorCodes.PackageNameRequired)]
    [InlineData("Widget", DomainErrorCodes.PackageNameInvalid)]
    [InlineData("9widget", DomainErrorCodes.PackageNameInvalid)]
    [InlineData("widget/", DomainErrorCodes.PackageNameInvalid)]
    public void PackageName_RejectsInvalidNames(string value, string expectedCode)
    {
        var exception = Assert.Throws<DomainException>(() => PackageName.Create(value));

        Assert.Equal(expectedCode, exception.Code);
    }

    [Fact]
    public void PackageVersion_ParsesSemanticVersion()
    {
        var version = PackageVersion.Create("1.2.3-rc.1+build.9");

        Assert.Equal(1, version.Major);
        Assert.Equal(2, version.Minor);
        Assert.Equal(3, version.Patch);
        Assert.Equal("rc.1", version.PreRelease);
        Assert.Equal("build.9", version.BuildMetadata);
        Assert.Equal("1.2.3-rc.1+build.9", version.ToString());
    }

    [Theory]
    [InlineData("1.2")]
    [InlineData("01.2.3")]
    [InlineData("1.2.3.4")]
    public void PackageVersion_RejectsInvalidVersions(string value)
    {
        var exception = Assert.Throws<DomainException>(() => PackageVersion.Create(value));

        Assert.Equal(DomainErrorCodes.PackageVersionInvalid, exception.Code);
    }

    [Fact]
    public void PackageSource_NormalizesCommitAndRejectsInsecureUrls()
    {
        var source = PackageSource.Create(TestPackages.SourceUrlValue, TestPackages.CommitSha.ToUpperInvariant());

        Assert.Equal(TestPackages.CommitSha, source.CommitSha);
        Assert.Equal("github.com", source.Host);

        var insecure = Assert.Throws<DomainException>(
            () => PackageSource.Create("http://github.com/example/widgets", TestPackages.CommitSha));
        Assert.Equal(DomainErrorCodes.SourceUrlInvalid, insecure.Code);
    }

    [Fact]
    public void ContentHash_RequiresSha256AndNormalizesCase()
    {
        Assert.Equal(TestPackages.ContentHashValue, ContentHash.Create(TestPackages.ContentHashValue.ToUpperInvariant()).Value);

        var exception = Assert.Throws<DomainException>(() => ContentHash.Create("abc"));
        Assert.Equal(DomainErrorCodes.ContentHashInvalid, exception.Code);
    }

    [Fact]
    public void BuildPolicy_EnforcesTimeoutBounds()
    {
        var policy = BuildPolicy.Create(BuildIsolation.Container, false, TimeSpan.FromMinutes(5));

        Assert.Equal(BuildIsolation.Container, policy.Isolation);
        Assert.False(policy.AllowNetwork);

        Assert.Throws<DomainException>(
            () => BuildPolicy.Create(BuildIsolation.Container, false, TimeSpan.Zero));
        Assert.Throws<DomainException>(
            () => BuildPolicy.Create(BuildIsolation.Container, false, TimeSpan.FromHours(2)));
    }

    [Fact]
    public void Signature_PendingIsNotPublishableAndSignedIs()
    {
        Assert.False(PackageSignature.Pending.IsPublishable);
        Assert.False(PackageSignature.Unsigned.IsPublishable);

        var signed = PackageSignature.Signed("ed25519", "c2lnbmF0dXJl");

        Assert.True(signed.IsPublishable);
        signed.EnsurePublishable();

        var exception = Assert.Throws<DomainException>(PackageSignature.Pending.EnsurePublishable);
        Assert.Equal(DomainErrorCodes.SignaturePending, exception.Code);
    }

    [Fact]
    public void Manifest_ReportsPublishabilityFromSignature()
    {
        Assert.False(TestPackages.CreateManifest().IsPublishable);
        Assert.True(TestPackages.CreateManifest(TestPackages.CreateSignedSignature()).IsPublishable);
    }

    [Fact]
    public void Toolchain_RequiresNameAndVersion()
    {
        var toolchain = Toolchain.Create("gcc", "13.2.0");

        Assert.Equal("gcc", toolchain.Name);
        Assert.Throws<DomainException>(() => Toolchain.Create("", "13.2.0"));
    }
}
