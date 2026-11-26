using Microsoft.Extensions.Logging.Abstractions;
using Pkgsrv.Application.Interfaces.Events;
using Pkgsrv.Application.Interfaces.Time;
using Pkgsrv.Domain.Events;
using Pkgsrv.Infrastructure.Events.Outbox;
using Pkgsrv.Infrastructure.Persistence.Postgres;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PackageOutboxDispatcherTests
{
    private static readonly DateTimeOffset Now =
        new(2026, 2, 3, 4, 5, 6, TimeSpan.Zero);

    [Fact]
    public async Task Dispatcher_PublishesAndMarksDelivered()
    {
        var outboxEvent = CreateOutboxEvent();
        var store = new FakeOutboxStore(outboxEvent);
        var publisher = new RecordingOutboxPublisher();
        var dispatcher = CreateDispatcher(store, publisher, new FixedClock(Now));

        var claimed = await dispatcher.DispatchPendingAsync(CancellationToken.None);

        Assert.Equal(1, claimed);
        var published = Assert.Single(publisher.Messages);
        Assert.Equal(outboxEvent.IdempotencyKey, published.IdempotencyKey);
        Assert.IsType<PackageRegistered>(Assert.Single(published.Events));
        Assert.Equal(PackageOutboxStatus.Delivered, store.Messages.Single().Status);
    }

    [Fact]
    public async Task Dispatcher_FailureRetainsPendingRowAndSchedulesBackoff()
    {
        var outboxEvent = CreateOutboxEvent();
        var store = new FakeOutboxStore(outboxEvent);
        var publisher = new RecordingOutboxPublisher { Throw = true };
        var dispatcher = CreateDispatcher(store, publisher, new FixedClock(Now));

        await dispatcher.DispatchPendingAsync(CancellationToken.None);

        var stored = Assert.Single(store.Messages);
        Assert.Equal(PackageOutboxStatus.Pending, stored.Status);
        Assert.Equal(Now + TimeSpan.FromSeconds(5), stored.AvailableAt);
        Assert.Equal("InvalidOperationException", stored.LastError);
        Assert.Null(stored.ClaimedAt);
        Assert.Null(stored.ClaimId);
        Assert.False(stored.Exhausted);
    }

    [Fact]
    public async Task Dispatcher_ExhaustedFailureIsRetainedAsFailed()
    {
        var outboxEvent = CreateOutboxEvent();
        var store = new FakeOutboxStore(outboxEvent);
        var publisher = new RecordingOutboxPublisher { Throw = true };
        var dispatcher = CreateDispatcher(
            store,
            publisher,
            new FixedClock(Now),
            new PackageOutboxDispatcherOptions { MaxAttempts = 1 });

        await dispatcher.DispatchPendingAsync(CancellationToken.None);

        var stored = Assert.Single(store.Messages);
        Assert.Equal(PackageOutboxStatus.Failed, stored.Status);
        Assert.True(stored.Exhausted);
        Assert.Equal(Now, stored.AvailableAt);
    }

    [Fact]
    public async Task Dispatcher_MarksDeliveredOnlyAfterPublisherSucceeds()
    {
        var outboxEvent = CreateOutboxEvent();
        var store = new FakeOutboxStore(outboxEvent);
        var publisher = new RecordingOutboxPublisher
        {
            OnPublish = () => Assert.Equal(
                PackageOutboxStatus.Pending,
                store.Messages.Single().Status)
        };
        var dispatcher = CreateDispatcher(store, publisher, new FixedClock(Now));

        await dispatcher.DispatchPendingAsync(CancellationToken.None);

        Assert.Equal(PackageOutboxStatus.Delivered, store.Messages.Single().Status);
    }

    [Fact]
    public async Task Dispatcher_ReplaysPendingRowAfterRestart()
    {
        var outboxEvent = CreateOutboxEvent();
        var store = new FakeOutboxStore(outboxEvent);
        var failingPublisher = new RecordingOutboxPublisher { Throw = true };
        var firstDispatcher = CreateDispatcher(store, failingPublisher, new FixedClock(Now));

        await firstDispatcher.DispatchPendingAsync(CancellationToken.None);

        var retryAt = store.Messages.Single().AvailableAt;
        var succeedingPublisher = new RecordingOutboxPublisher();
        var restartedDispatcher = CreateDispatcher(
            store,
            succeedingPublisher,
            new FixedClock(retryAt + TimeSpan.FromTicks(1)));

        var claimed = await restartedDispatcher.DispatchPendingAsync(CancellationToken.None);

        Assert.Equal(1, claimed);
        Assert.Equal(outboxEvent.IdempotencyKey, Assert.Single(succeedingPublisher.Messages).IdempotencyKey);
        var stored = Assert.Single(store.Messages);
        Assert.Equal(PackageOutboxStatus.Delivered, stored.Status);
        Assert.Equal(2, stored.AttemptCount);
    }

    [Fact]
    public async Task Dispatcher_DoesNotReclaimDeliveredRows()
    {
        var outboxEvent = CreateOutboxEvent();
        var store = new FakeOutboxStore(outboxEvent);
        store.Messages.Single().Status = PackageOutboxStatus.Delivered;
        var publisher = new RecordingOutboxPublisher();
        var dispatcher = CreateDispatcher(store, publisher, new FixedClock(Now));

        var claimed = await dispatcher.DispatchPendingAsync(CancellationToken.None);

        Assert.Equal(0, claimed);
        Assert.Empty(publisher.Messages);
    }

    private static PackageOutboxDispatcher CreateDispatcher(
        FakeOutboxStore store,
        RecordingOutboxPublisher publisher,
        ISystemClock clock,
        PackageOutboxDispatcherOptions? options = null) =>
        new(
            store,
            publisher,
            clock,
            options ?? new PackageOutboxDispatcherOptions(),
            NullLogger<PackageOutboxDispatcher>.Instance);

    private static PostgresOutboxEvent CreateOutboxEvent()
    {
        var package = TestPackages.CreatePackage();
        return PostgresOutboxEventSerializer.ToOutboxEvent(
            Assert.Single(package.PeekDomainEvents()));
    }

    private sealed class FixedClock : ISystemClock
    {
        public FixedClock(DateTimeOffset utcNow)
        {
            UtcNow = utcNow;
        }

        public DateTimeOffset UtcNow { get; }
    }

    private sealed class RecordingOutboxPublisher : IOutboxEventPublisher
    {
        public List<OutboxEventMessage> Messages { get; } = new();

        public bool Throw { get; init; }

        public Action? OnPublish { get; init; }

        public Task PublishAsync(
            OutboxEventMessage message,
            CancellationToken cancellationToken)
        {
            OnPublish?.Invoke();

            if (Throw)
            {
                throw new InvalidOperationException("outbox publication failed");
            }

            Messages.Add(message);
            return Task.CompletedTask;
        }
    }

    private sealed class FakeOutboxStore : IPackageOutboxStore
    {
        public FakeOutboxStore(PostgresOutboxEvent outboxEvent)
        {
            Messages.Add(new StoredOutboxMessage(outboxEvent, Now));
        }

        public List<StoredOutboxMessage> Messages { get; } = new();

        public Task<IReadOnlyList<PackageOutboxMessage>> ClaimAsync(
            DateTimeOffset now,
            int maxCount,
            TimeSpan lease,
            CancellationToken cancellationToken)
        {
            var claimId = Guid.NewGuid().ToString("N");
            var claimed = Messages
                .Where(message => message.Status == PackageOutboxStatus.Pending
                    && message.AvailableAt <= now
                    && (message.ClaimedAt is null || message.ClaimedAt < now - lease))
                .OrderBy(message => message.AvailableAt)
                .Take(maxCount)
                .ToList();

            foreach (var message in claimed)
            {
                message.ClaimedAt = now;
                message.ClaimId = claimId;
                message.AttemptCount++;
            }

            return Task.FromResult<IReadOnlyList<PackageOutboxMessage>>(
                claimed
                    .Select(message => new PackageOutboxMessage(
                        message.OutboxEvent.IdempotencyKey,
                        message.OutboxEvent.PackageName,
                        message.OutboxEvent.PackageVersion,
                        message.OutboxEvent.EventType,
                        message.OutboxEvent.Payload,
                        message.OutboxEvent.OccurredAt,
                        message.AttemptCount,
                        claimId))
                    .ToArray());
        }

        public Task MarkDeliveredAsync(
            string idempotencyKey,
            string claimId,
            CancellationToken cancellationToken)
        {
            var message = Messages.Single(candidate =>
                candidate.OutboxEvent.IdempotencyKey == idempotencyKey
                && candidate.ClaimId == claimId);

            message.Status = PackageOutboxStatus.Delivered;
            message.ClaimedAt = null;
            message.ClaimId = null;
            message.LastError = null;
            return Task.CompletedTask;
        }

        public Task MarkFailedAsync(
            string idempotencyKey,
            string claimId,
            int attemptCount,
            DateTimeOffset nextAvailableAt,
            bool exhausted,
            string error,
            CancellationToken cancellationToken)
        {
            var message = Messages.Single(candidate =>
                candidate.OutboxEvent.IdempotencyKey == idempotencyKey
                && candidate.ClaimId == claimId
                && candidate.AttemptCount == attemptCount);

            message.Status = exhausted
                ? PackageOutboxStatus.Failed
                : PackageOutboxStatus.Pending;
            message.ClaimedAt = null;
            message.ClaimId = null;
            message.AvailableAt = nextAvailableAt;
            message.LastError = error;
            message.Exhausted = exhausted;
            return Task.CompletedTask;
        }
    }

    private sealed class StoredOutboxMessage
    {
        public StoredOutboxMessage(PostgresOutboxEvent outboxEvent, DateTimeOffset availableAt)
        {
            OutboxEvent = outboxEvent;
            AvailableAt = availableAt;
        }

        public PostgresOutboxEvent OutboxEvent { get; }

        public DateTimeOffset AvailableAt { get; set; }

        public DateTimeOffset? ClaimedAt { get; set; }

        public string? ClaimId { get; set; }

        public int AttemptCount { get; set; }

        public PackageOutboxStatus Status { get; set; } = PackageOutboxStatus.Pending;

        public string? LastError { get; set; }

        public bool Exhausted { get; set; }
    }
}
