namespace Pkgsrv.Application.Interfaces.Persistence;

public enum StorageProviderKind
{
    InMemory = 0,
    Postgres = 1
}

public sealed record StorageReadinessReport(
    bool IsReady,
    StorageProviderKind Provider,
    bool IsDurable,
    int? SchemaVersion,
    bool? PostgresAvailable);

public interface IStorageReadinessProbe
{
    Task<StorageReadinessReport> ProbeAsync(CancellationToken cancellationToken);
}
