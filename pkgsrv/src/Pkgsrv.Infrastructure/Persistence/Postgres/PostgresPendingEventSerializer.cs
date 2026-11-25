using System.Text.Json;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public static class PostgresPendingEventSerializer
{
    private const string RegisteredType = "PackageRegistered";
    private const string BuildRequestedType = "PackageBuildRequested";

    private static readonly JsonSerializerOptions Options =
        new(JsonSerializerDefaults.Web);

    public static string Serialize(IReadOnlyCollection<IDomainEvent> events)
    {
        ArgumentNullException.ThrowIfNull(events);

        return SerializeDescriptors(events.Select(ToDescriptor).ToArray());
    }

    public static string RemoveAcknowledged(
        string? json,
        IReadOnlyCollection<IDomainEvent> publishedEvents)
    {
        ArgumentNullException.ThrowIfNull(publishedEvents);

        var descriptors = DeserializeDescriptors(json);
        var acknowledged = publishedEvents
            .Select(ToDescriptor)
            .Select(Identity)
            .ToHashSet(StringComparer.Ordinal);
        var remaining = descriptors
            .Where(descriptor => !acknowledged.Contains(Identity(descriptor)))
            .ToArray();

        return SerializeDescriptors(remaining);
    }

    public static IReadOnlyList<IDomainEvent> Deserialize(
        string? json,
        PackageManifest manifest,
        BuildPolicy buildPolicy)
    {
        ArgumentNullException.ThrowIfNull(manifest);
        ArgumentNullException.ThrowIfNull(buildPolicy);

        var descriptors = DeserializeDescriptors(json);

        return descriptors.Select(descriptor => descriptor.Type switch
        {
            RegisteredType => (IDomainEvent)new PackageRegistered(
                PackageKey.Create(descriptor.Name, descriptor.Version),
                manifest,
                descriptor.OccurredAt),
            BuildRequestedType => new PackageBuildRequested(
                PackageKey.Create(descriptor.Name, descriptor.Version),
                buildPolicy,
                descriptor.OccurredAt),
            _ => throw new InvalidOperationException(
                $"Pending package event type '{descriptor.Type}' is not supported.")
        }).ToArray();
    }

    private static PendingEventDescriptor ToDescriptor(IDomainEvent domainEvent) => domainEvent switch
    {
        PackageRegistered registered => new PendingEventDescriptor(
            RegisteredType,
            registered.Key.Name.Value,
            registered.Key.Version.ToString(),
            registered.OccurredAt),
        PackageBuildRequested buildRequested => new PendingEventDescriptor(
            BuildRequestedType,
            buildRequested.Key.Name.Value,
            buildRequested.Key.Version.ToString(),
            buildRequested.OccurredAt),
        _ => throw new InvalidOperationException(
            $"Domain event type '{domainEvent.GetType().FullName}' is not supported by the PostgreSQL outbox.")
    };

    private static PendingEventDescriptor[] DeserializeDescriptors(string? json)
    {
        if (string.IsNullOrWhiteSpace(json))
        {
            return Array.Empty<PendingEventDescriptor>();
        }

        return JsonSerializer.Deserialize<PendingEventDescriptor[]>(json, Options)
            ?? throw new InvalidOperationException("Pending package events payload is invalid.");
    }

    private static string SerializeDescriptors(IReadOnlyCollection<PendingEventDescriptor> descriptors) =>
        JsonSerializer.Serialize(descriptors, Options);

    private static string Identity(PendingEventDescriptor descriptor) =>
        $"{descriptor.Type}|{descriptor.Name}|{descriptor.Version}|{descriptor.OccurredAt.UtcDateTime.Ticks}";

    private sealed record PendingEventDescriptor(
        string Type,
        string Name,
        string Version,
        DateTimeOffset OccurredAt);
}
