namespace Pkgsrv.Infrastructure.Postgres;

public sealed record PostgresPackageStoreOptions
{
    public const int DefaultMaxListResults = 1000;
    public const int MaximumListResults = 10000;

    public PostgresPackageStoreOptions(string connectionString, int maxListResults = DefaultMaxListResults)
    {
        if (string.IsNullOrWhiteSpace(connectionString))
        {
            throw new ArgumentException(
                "A PostgreSQL connection string is required.",
                nameof(connectionString));
        }

        if (maxListResults <= 0 || maxListResults > MaximumListResults)
        {
            throw new ArgumentOutOfRangeException(
                nameof(maxListResults),
                $"The maximum list result count must be between 1 and {MaximumListResults}.");
        }

        ConnectionString = connectionString;
        MaxListResults = maxListResults;
    }

    public string ConnectionString { get; }

    public int MaxListResults { get; }
}
