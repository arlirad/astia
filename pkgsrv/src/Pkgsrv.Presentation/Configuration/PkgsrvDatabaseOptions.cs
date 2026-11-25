using Microsoft.Extensions.Configuration;

namespace Pkgsrv.Presentation.Configuration;

public enum PkgsrvDatabaseProvider
{
    InMemory = 0,
    Postgres = 1
}

public sealed record PkgsrvDatabaseOptions
{
    private PkgsrvDatabaseOptions(
        PkgsrvDatabaseProvider provider,
        string? connectionString,
        bool initializeSchema)
    {
        Provider = provider;
        ConnectionString = connectionString;
        InitializeSchema = initializeSchema;
    }

    public PkgsrvDatabaseProvider Provider { get; }

    public string? ConnectionString { get; }

    public bool InitializeSchema { get; }

    public static PkgsrvDatabaseOptions InMemory { get; } =
        new(PkgsrvDatabaseProvider.InMemory, null, false);

    public static PkgsrvDatabaseOptions FromConfiguration(IConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(configuration);

        var section = configuration.GetSection("Pkgsrv:Database");
        var providerValue = section["Provider"];
        var connectionRaw = section["ConnectionString"];
        var connectionString = connectionRaw?.Trim();

        if (connectionRaw is not null && string.IsNullOrWhiteSpace(connectionRaw))
        {
            throw new InvalidOperationException(
                "Pkgsrv:Database:ConnectionString was supplied but is empty. Omit it for in-memory mode.");
        }

        var provider = providerValue is null
            ? (string.IsNullOrWhiteSpace(connectionString)
                ? PkgsrvDatabaseProvider.InMemory
                : PkgsrvDatabaseProvider.Postgres)
            : ParseProvider(providerValue);

        if (provider == PkgsrvDatabaseProvider.Postgres
            && string.IsNullOrWhiteSpace(connectionString))
        {
            throw new InvalidOperationException(
                "Pkgsrv:Database:ConnectionString is required when Provider is Postgres.");
        }

        var initializeSchemaRaw = section["InitializeSchema"];
        var initializeSchema = false;

        if (initializeSchemaRaw is not null
            && (string.IsNullOrWhiteSpace(initializeSchemaRaw)
                || !bool.TryParse(initializeSchemaRaw, out initializeSchema)))
        {
            throw new InvalidOperationException(
                "Pkgsrv:Database:InitializeSchema must be true or false when supplied.");
        }

        if (initializeSchema && provider != PkgsrvDatabaseProvider.Postgres)
        {
            throw new InvalidOperationException(
                "Pkgsrv:Database:InitializeSchema requires Provider=Postgres.");
        }

        return new PkgsrvDatabaseOptions(provider, connectionString, initializeSchema);
    }

    private static PkgsrvDatabaseProvider ParseProvider(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new InvalidOperationException(
                "Pkgsrv:Database:Provider was supplied but is empty. Use InMemory or Postgres.");
        }

        if (value.Trim().Equals("InMemory", StringComparison.OrdinalIgnoreCase))
        {
            return PkgsrvDatabaseProvider.InMemory;
        }

        if (value.Trim().Equals("Postgres", StringComparison.OrdinalIgnoreCase)
            || value.Trim().Equals("PostgreSQL", StringComparison.OrdinalIgnoreCase))
        {
            return PkgsrvDatabaseProvider.Postgres;
        }

        throw new InvalidOperationException(
            $"Pkgsrv:Database:Provider '{value}' is invalid. Use InMemory or Postgres.");
    }
}
