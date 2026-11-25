using Microsoft.EntityFrameworkCore;
using Npgsql;
using Pkgsrv.Infrastructure.Persistence.Postgres;

namespace Pkgsrv.Infrastructure.Tests;

[AttributeUsage(AttributeTargets.Method)]
public sealed class PostgresFactAttribute : FactAttribute
{
    public PostgresFactAttribute()
    {
        if (string.IsNullOrWhiteSpace(
                Environment.GetEnvironmentVariable(PostgresTestDatabase.EnvironmentVariable)))
        {
            Skip = $"Set {PostgresTestDatabase.EnvironmentVariable} to run PostgreSQL integration tests.";
        }
    }
}

internal sealed class PostgresTestDatabase : IAsyncDisposable
{
    public const string EnvironmentVariable = "PKGSRV_TEST_POSTGRES_CONNECTION";

    private readonly string _adminConnectionString;
    private readonly string _schema;

    private PostgresTestDatabase(
        string adminConnectionString,
        string schema,
        string connectionString)
    {
        _adminConnectionString = adminConnectionString;
        _schema = schema;
        ConnectionString = connectionString;
        Options = new DbContextOptionsBuilder<PostgresPackageDbContext>()
            .UseNpgsql(connectionString)
            .Options;
    }

    public string ConnectionString { get; }

    public DbContextOptions<PostgresPackageDbContext> Options { get; }

    public static async Task<PostgresTestDatabase> CreateAsync(CancellationToken cancellationToken)
    {
        var baseConnectionString = Environment.GetEnvironmentVariable(EnvironmentVariable);

        if (string.IsNullOrWhiteSpace(baseConnectionString))
        {
            throw new InvalidOperationException(
                $"{EnvironmentVariable} is required to run PostgreSQL integration tests.");
        }

        var schema = $"pkgsrv_it_{Guid.NewGuid():N}";
        await using (var connection = new NpgsqlConnection(baseConnectionString))
        {
            await connection.OpenAsync(cancellationToken).ConfigureAwait(false);
            await using var command = new NpgsqlCommand(
                $"CREATE SCHEMA \"{schema}\"",
                connection);
            await command.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);
        }

        var builder = new NpgsqlConnectionStringBuilder(baseConnectionString)
        {
            SearchPath = schema
        };

        return new PostgresTestDatabase(baseConnectionString, schema, builder.ConnectionString);
    }

    public PostgresPackageDbContext CreateContext() => new(Options);

    public IDbContextFactory<PostgresPackageDbContext> CreateFactory() =>
        new TestDbContextFactory(Options);

    public async ValueTask DisposeAsync()
    {
        await using var connection = new NpgsqlConnection(_adminConnectionString);
        await connection.OpenAsync().ConfigureAwait(false);

        await using var command = new NpgsqlCommand(
            $"DROP SCHEMA IF EXISTS \"{_schema}\" CASCADE",
            connection);

        await command.ExecuteNonQueryAsync().ConfigureAwait(false);
    }

    private sealed class TestDbContextFactory : IDbContextFactory<PostgresPackageDbContext>
    {
        private readonly DbContextOptions<PostgresPackageDbContext> _options;

        public TestDbContextFactory(DbContextOptions<PostgresPackageDbContext> options)
        {
            _options = options;
        }

        public PostgresPackageDbContext CreateDbContext() => new(_options);
    }
}
