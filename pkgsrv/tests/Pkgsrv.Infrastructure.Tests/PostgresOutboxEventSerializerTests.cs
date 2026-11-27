using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Persistence.Postgres;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresOutboxEventSerializerTests
{
    [Fact]
    public void RegisteredEvent_SerializesDeterministicallyWithStableIdempotencyKey()
    {
        var package = TestPackages.CreatePackage();
        var domainEvent = Assert.Single(package.PeekDomainEvents());

        var first = PostgresOutboxEventSerializer.ToOutboxEvent(domainEvent);
        var second = PostgresOutboxEventSerializer.ToOutboxEvent(domainEvent);

        Assert.Equal(PostgresOutboxEventSerializer.RegisteredEventType, first.EventType);
        Assert.Equal(package.Key.Name.Value, first.PackageName);
        Assert.Equal(package.Key.Version.ToString(), first.PackageVersion);
        Assert.Equal(package.RegisteredAt, first.OccurredAt);
        Assert.Equal(first.IdempotencyKey, second.IdempotencyKey);
        Assert.Equal(first.Payload, second.Payload);
        Assert.DoesNotContain("token", first.Payload, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("password", first.Payload, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void RegisteredEvent_RoundTripsThroughPayload()
    {
        var package = TestPackages.CreatePackage();
        var original = Assert.IsType<PackageRegistered>(
            Assert.Single(package.PeekDomainEvents()));
        var outboxEvent = PostgresOutboxEventSerializer.ToOutboxEvent(original);

        var rehydrated = Assert.IsType<PackageRegistered>(
            Assert.Single(PostgresOutboxEventSerializer.Deserialize(
                outboxEvent.EventType,
                outboxEvent.Payload)));

        Assert.Equal(original.Key, rehydrated.Key);
        Assert.Equal(original.OccurredAt, rehydrated.OccurredAt);
        Assert.Equal(original.Manifest.Name, rehydrated.Manifest.Name);
        Assert.Equal(original.Manifest.Version, rehydrated.Manifest.Version);
        Assert.Equal(original.Manifest.Source, rehydrated.Manifest.Source);
        Assert.Equal(original.Manifest.Toolchain, rehydrated.Manifest.Toolchain);
        Assert.Equal(original.Manifest.ContentHash, rehydrated.Manifest.ContentHash);
        Assert.Equal(original.Manifest.Provenance, rehydrated.Manifest.Provenance);
        Assert.Equal(original.Manifest.Signature, rehydrated.Manifest.Signature);
    }

    [Fact]
    public void BuildRequestedEvent_RoundTripsThroughPayload()
    {
        var package = TestPackages.CreatePackage();
        package.RequestBuild(TestPackages.Timestamp.AddMinutes(1));
        var original = Assert.IsType<PackageBuildRequested>(
            Assert.Single(package.PeekDomainEvents().OfType<PackageBuildRequested>()));
        var outboxEvent = PostgresOutboxEventSerializer.ToOutboxEvent(original);

        Assert.Equal(PostgresOutboxEventSerializer.BuildRequestedEventType, outboxEvent.EventType);

        var rehydrated = Assert.IsType<PackageBuildRequested>(
            Assert.Single(PostgresOutboxEventSerializer.Deserialize(
                outboxEvent.EventType,
                outboxEvent.Payload)));

        Assert.Equal(original.Key, rehydrated.Key);
        Assert.Equal(original.OccurredAt, rehydrated.OccurredAt);
        Assert.Equal(original.BuildPolicy, rehydrated.BuildPolicy);
    }

    [Fact]
    public void DifferentEvents_UseDifferentIdempotencyKeys()
    {
        var registered = TestPackages.CreatePackage();
        var buildRequested = TestPackages.CreatePackage();
        buildRequested.RequestBuild(TestPackages.Timestamp.AddMinutes(1));

        var registeredOutbox = PostgresOutboxEventSerializer.ToOutboxEvent(
            Assert.Single(registered.PeekDomainEvents()));
        var buildOutbox = PostgresOutboxEventSerializer.ToOutboxEvent(
            Assert.Single(buildRequested.PeekDomainEvents().OfType<PackageBuildRequested>()));

        Assert.NotEqual(registeredOutbox.IdempotencyKey, buildOutbox.IdempotencyKey);
    }

    [Fact]
    public void UnsupportedEvent_IsRejectedByOutboxSerializer()
    {
        var exception = Assert.Throws<InvalidOperationException>(
            () => PostgresOutboxEventSerializer.ToOutboxEvent(
                new UnsupportedDomainEvent(TestPackages.Timestamp)));

        Assert.Contains("not supported", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void UnsupportedEventType_IsRejectedDuringDeserialization()
    {
        var exception = Assert.Throws<InvalidOperationException>(
            () => PostgresOutboxEventSerializer.Deserialize(
                "Unsupported",
                "{}"));

        Assert.Contains("not supported", exception.Message, StringComparison.Ordinal);
    }

    private sealed record UnsupportedDomainEvent(DateTimeOffset OccurredAt) : IDomainEvent;
}
