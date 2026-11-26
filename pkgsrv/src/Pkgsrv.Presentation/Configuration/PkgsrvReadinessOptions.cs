using Microsoft.Extensions.Configuration;

namespace Pkgsrv.Presentation.Configuration;

public sealed record PkgsrvReadinessOptions
{
    public const int DefaultPostgresProbeTimeoutMilliseconds = 2000;
    public const int MaximumPostgresProbeTimeoutMilliseconds = 10_000;

    private PkgsrvReadinessOptions(TimeSpan postgresProbeTimeout)
    {
        PostgresProbeTimeout = postgresProbeTimeout;
    }

    public TimeSpan PostgresProbeTimeout { get; }

    public static PkgsrvReadinessOptions Default { get; } =
        new(TimeSpan.FromMilliseconds(DefaultPostgresProbeTimeoutMilliseconds));

    public static PkgsrvReadinessOptions Create(TimeSpan postgresProbeTimeout)
    {
        if (postgresProbeTimeout <= TimeSpan.Zero
            || postgresProbeTimeout > TimeSpan.FromMilliseconds(MaximumPostgresProbeTimeoutMilliseconds))
        {
            throw new ArgumentOutOfRangeException(
                nameof(postgresProbeTimeout),
                $"Readiness probe timeout must be between 1 and {MaximumPostgresProbeTimeoutMilliseconds} milliseconds.");
        }

        return new PkgsrvReadinessOptions(postgresProbeTimeout);
    }

    public static PkgsrvReadinessOptions FromConfiguration(IConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(configuration);

        var raw = configuration["Pkgsrv:Readiness:PostgresProbeTimeoutMs"];

        if (raw is null)
        {
            return Default;
        }

        if (string.IsNullOrWhiteSpace(raw)
            || !int.TryParse(raw, out var milliseconds)
            || milliseconds <= 0
            || milliseconds > MaximumPostgresProbeTimeoutMilliseconds)
        {
            throw new InvalidOperationException(
                $"Pkgsrv:Readiness:PostgresProbeTimeoutMs must be an integer between 1 and {MaximumPostgresProbeTimeoutMilliseconds}.");
        }

        return new PkgsrvReadinessOptions(TimeSpan.FromMilliseconds(milliseconds));
    }
}
