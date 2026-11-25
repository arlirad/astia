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
    [InlineData(2)]
    [InlineData(99)]
    public void EnsureSupportedVersion_RejectsOtherVersions(int version)
    {
        Assert.Throws<InvalidOperationException>(
            () => PostgresPackageSchemaValidator.EnsureSupportedVersion(version));
    }
}
