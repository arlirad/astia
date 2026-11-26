using System.Diagnostics;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Infrastructure.Persistence;
using Pkgsrv.Infrastructure.Persistence.Postgres;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class StorageReadinessProbeTests
{
    [Fact]
    public async Task InMemoryProbe_IsReadyButNotDurable()
    {
        var report = await new InMemoryStorageReadinessProbe()
            .ProbeAsync(CancellationToken.None);

        Assert.True(report.IsReady);
        Assert.Equal(StorageProviderKind.InMemory, report.Provider);
        Assert.False(report.IsDurable);
        Assert.Null(report.SchemaVersion);
        Assert.Null(report.PostgresAvailable);
    }

    [Fact]
    public async Task PostgresProbe_CurrentSchemaIsReady()
    {
        var probe = CreateProbe(new PostgresSchemaState(
            Connected: true,
            AppliedMigrationCount: 3,
            IsCurrent: true));

        var report = await probe.ProbeAsync(CancellationToken.None);

        Assert.True(report.IsReady);
        Assert.Equal(StorageProviderKind.Postgres, report.Provider);
        Assert.True(report.IsDurable);
        Assert.Equal(3, report.SchemaVersion);
        Assert.True(report.PostgresAvailable);
    }

    [Fact]
    public async Task PostgresProbe_PendingMigrationsAreNotReadyButAvailable()
    {
        var probe = CreateProbe(new PostgresSchemaState(
            Connected: true,
            AppliedMigrationCount: 2,
            IsCurrent: false));

        var report = await probe.ProbeAsync(CancellationToken.None);

        Assert.False(report.IsReady);
        Assert.Equal(StorageProviderKind.Postgres, report.Provider);
        Assert.Equal(2, report.SchemaVersion);
        Assert.True(report.PostgresAvailable);
    }

    [Fact]
    public async Task PostgresProbe_UnknownMigrationsAreNotCurrent()
    {
        var probe = CreateProbe(new PostgresSchemaState(
            Connected: true,
            AppliedMigrationCount: 4,
            IsCurrent: false));

        var report = await probe.ProbeAsync(CancellationToken.None);

        Assert.False(report.IsReady);
        Assert.Equal(4, report.SchemaVersion);
        Assert.True(report.PostgresAvailable);
    }

    [Fact]
    public async Task PostgresProbe_UnavailableDatabaseIsNotReady()
    {
        var probe = CreateProbe(new PostgresSchemaState(
            Connected: false,
            AppliedMigrationCount: 0,
            IsCurrent: false));

        var report = await probe.ProbeAsync(CancellationToken.None);

        Assert.False(report.IsReady);
        Assert.Null(report.SchemaVersion);
        Assert.False(report.PostgresAvailable);
    }

    [Fact]
    public async Task PostgresProbe_TimeoutIsBoundedAndNotReady()
    {
        var probe = new PostgresStorageReadinessProbe(
            new FakeSchemaStateReader(async cancellationToken =>
            {
                await Task.Delay(Timeout.InfiniteTimeSpan, cancellationToken);
                return new PostgresSchemaState(true, 3, true);
            }),
            TimeSpan.FromMilliseconds(50));
        var stopwatch = Stopwatch.StartNew();

        var report = await probe.ProbeAsync(CancellationToken.None);

        stopwatch.Stop();
        Assert.True(stopwatch.Elapsed < TimeSpan.FromSeconds(5));
        Assert.False(report.IsReady);
        Assert.Null(report.SchemaVersion);
        Assert.False(report.PostgresAvailable);
    }

    [Fact]
    public async Task PostgresProbe_FailureDoesNotLeakExceptionDetails()
    {
        var probe = new PostgresStorageReadinessProbe(
            new FakeSchemaStateReader(_ =>
                throw new InvalidOperationException(
                    "Host=secret-db.internal;Password=supersecret")),
            TimeSpan.FromSeconds(1));

        var report = await probe.ProbeAsync(CancellationToken.None);

        Assert.False(report.IsReady);
        Assert.Null(report.SchemaVersion);
        Assert.False(report.PostgresAvailable);
    }

    [Fact]
    public async Task PostgresProbe_ExternalCancellationPropagates()
    {
        var probe = new PostgresStorageReadinessProbe(
            new FakeSchemaStateReader(async cancellationToken =>
            {
                await Task.Delay(Timeout.InfiniteTimeSpan, cancellationToken);
                return new PostgresSchemaState(true, 3, true);
            }),
            TimeSpan.FromSeconds(1));
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            () => probe.ProbeAsync(cancellation.Token));
    }

    private static PostgresStorageReadinessProbe CreateProbe(PostgresSchemaState state) =>
        new(
            new FakeSchemaStateReader(_ => Task.FromResult(state)),
            TimeSpan.FromSeconds(1));

    private sealed class FakeSchemaStateReader : IPostgresSchemaStateReader
    {
        private readonly Func<CancellationToken, Task<PostgresSchemaState>> _read;

        public FakeSchemaStateReader(
            Func<CancellationToken, Task<PostgresSchemaState>> read)
        {
            _read = read;
        }

        public Task<PostgresSchemaState> ReadAsync(CancellationToken cancellationToken) =>
            _read(cancellationToken);
    }
}
