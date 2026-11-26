using Pkgsrv.Infrastructure.Persistence.Postgres;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPackageSchemaValidatorTests
{
    [Fact]
    public void EnsureSupportedVersion_AcceptsRequiredVersion()
    {
        PostgresPackageSchemaValidator.EnsureSupportedVersion(
            PostgresPackageSchema.RequiredVersion);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(99)]
    public void EnsureSupportedVersion_RejectsOldNewAndFutureVersions(int version)
    {
        Assert.Throws<InvalidOperationException>(
            () => PostgresPackageSchemaValidator.EnsureSupportedVersion(version));
    }

    [Fact]
    public void EnsureSupportedShape_AcceptsCompleteColumnSet()
    {
        PostgresPackageSchemaValidator.EnsureSupportedShape(
            Array.Empty<string>());
    }

    [Fact]
    public void EnsureSupportedShape_RejectsMissingVersionColumns()
    {
        var exception = Assert.Throws<InvalidOperationException>(
            () => PostgresPackageSchemaValidator.EnsureSupportedShape(
                ["VersionMajor", "VersionBuildMetadata"]));

        Assert.Contains("VersionMajor", exception.Message, StringComparison.Ordinal);
        Assert.Contains("VersionBuildMetadata", exception.Message, StringComparison.Ordinal);
    }
}
