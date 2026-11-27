using Pkgsrv.Application.Interfaces.Persistence;

namespace Pkgsrv.Infrastructure.Persistence;

public sealed class InMemoryStorageReadinessProbe : IStorageReadinessProbe
{
    public Task<StorageReadinessReport> ProbeAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        return Task.FromResult(
            new StorageReadinessReport(
                IsReady: true,
                Provider: StorageProviderKind.InMemory,
                IsDurable: false,
                SchemaVersion: null,
                PostgresAvailable: null));
    }
}
