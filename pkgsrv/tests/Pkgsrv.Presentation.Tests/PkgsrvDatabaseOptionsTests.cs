using Microsoft.Extensions.Configuration;
using Pkgsrv.Presentation.Configuration;

namespace Pkgsrv.Presentation.Tests;

public sealed class PkgsrvDatabaseOptionsTests
{
    [Fact]
    public void DefaultsToInMemoryWithoutDatabaseConfiguration()
    {
        var options = Resolve();

        Assert.Equal(PkgsrvDatabaseProvider.InMemory, options.Provider);
        Assert.Null(options.ConnectionString);
        Assert.False(options.InitializeSchema);
    }

    [Fact]
    public void ConnectionStringSelectsPostgres()
    {
        var options = Resolve(
            ("Pkgsrv:Database:ConnectionString", "Host=localhost;Database=pkgsrv;Username=pkgsrv;Password=placeholder"));

        Assert.Equal(PkgsrvDatabaseProvider.Postgres, options.Provider);
        Assert.Contains("Host=localhost", options.ConnectionString, StringComparison.Ordinal);
    }

    [Fact]
    public void ExplicitInMemoryWinsOverConnectionString()
    {
        var options = Resolve(
            ("Pkgsrv:Database:Provider", "InMemory"),
            ("Pkgsrv:Database:ConnectionString", "Host=localhost;Database=pkgsrv;Username=pkgsrv;Password=placeholder"));

        Assert.Equal(PkgsrvDatabaseProvider.InMemory, options.Provider);
    }

    [Fact]
    public void PostgresRequiresConnectionString()
    {
        var exception = Assert.Throws<InvalidOperationException>(
            () => Resolve(("Pkgsrv:Database:Provider", "Postgres")));

        Assert.Contains("ConnectionString", exception.Message, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData("")]
    [InlineData(" ")]
    [InlineData("Sqlite")]
    public void InvalidProviderIsRejected(string provider)
    {
        Assert.Throws<InvalidOperationException>(
            () => Resolve(("Pkgsrv:Database:Provider", provider)));
    }

    [Theory]
    [InlineData("")]
    [InlineData(" ")]
    public void EmptyConnectionStringIsRejected(string connectionString)
    {
        Assert.Throws<InvalidOperationException>(
            () => Resolve(("Pkgsrv:Database:ConnectionString", connectionString)));
    }

    [Fact]
    public void InitializeSchemaRequiresPostgres()
    {
        Assert.Throws<InvalidOperationException>(
            () => Resolve(("Pkgsrv:Database:InitializeSchema", "true")));
    }

    [Fact]
    public void InitializeSchemaCanBeEnabledForPostgres()
    {
        var options = Resolve(
            ("Pkgsrv:Database:Provider", "PostgreSQL"),
            ("Pkgsrv:Database:ConnectionString", "Host=localhost;Database=pkgsrv;Username=pkgsrv;Password=placeholder"),
            ("Pkgsrv:Database:InitializeSchema", "true"));

        Assert.Equal(PkgsrvDatabaseProvider.Postgres, options.Provider);
        Assert.True(options.InitializeSchema);
    }

    private static PkgsrvDatabaseOptions Resolve(params (string Key, string? Value)[] values) =>
        PkgsrvDatabaseOptions.FromConfiguration(
            new ConfigurationBuilder()
                .AddInMemoryCollection(values.Select(value =>
                    new KeyValuePair<string, string?>(value.Key, value.Value)))
                .Build());
}
