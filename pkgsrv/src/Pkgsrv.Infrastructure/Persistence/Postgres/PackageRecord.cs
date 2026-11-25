namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public sealed class PackageRecord
{
    public string Name { get; set; } = string.Empty;

    public string Version { get; set; } = string.Empty;

    public string SourceUrl { get; set; } = string.Empty;

    public string SourceCommit { get; set; } = string.Empty;

    public string ToolchainName { get; set; } = string.Empty;

    public string ToolchainVersion { get; set; } = string.Empty;

    public string ContentHash { get; set; } = string.Empty;

    public string Provenance { get; set; } = string.Empty;

    public int SignatureStatus { get; set; }

    public string? SignatureAlgorithm { get; set; }

    public string? SignatureValue { get; set; }

    public int BuildIsolation { get; set; }

    public bool AllowNetwork { get; set; }

    public int TimeoutSeconds { get; set; }

    public int State { get; set; }

    public string PendingEventsJson { get; set; } = "[]";

    public DateTimeOffset RegisteredAt { get; set; }

    public DateTimeOffset? BuildRequestedAt { get; set; }
}
