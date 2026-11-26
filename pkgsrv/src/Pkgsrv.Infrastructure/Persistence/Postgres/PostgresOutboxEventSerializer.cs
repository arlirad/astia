using System.Text.Json;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public sealed record PostgresOutboxEvent(
    string IdempotencyKey,
    string PackageName,
    string PackageVersion,
    string EventType,
    string Payload,
    DateTimeOffset OccurredAt);

public static class PostgresOutboxEventSerializer
{
    public const string RegisteredEventType = "PackageRegistered";
    public const string BuildRequestedEventType = "PackageBuildRequested";

    private static readonly JsonSerializerOptions Options =
        new(JsonSerializerDefaults.Web);

    public static PostgresOutboxEvent ToOutboxEvent(IDomainEvent domainEvent)
    {
        ArgumentNullException.ThrowIfNull(domainEvent);

        return domainEvent switch
        {
            PackageRegistered registered => Create(
                RegisteredEventType,
                registered.Key,
                registered.OccurredAt,
                new RegisteredPayload(
                    registered.Key.Name.Value,
                    registered.Key.Version.ToString(),
                    registered.Manifest.Source.RepositoryUri.ToString(),
                    registered.Manifest.Source.CommitSha,
                    registered.Manifest.Toolchain.Name,
                    registered.Manifest.Toolchain.Version,
                    registered.Manifest.ContentHash.Value,
                    registered.Manifest.Provenance.Value,
                    (int)registered.Manifest.Signature.Status,
                    registered.Manifest.Signature.Algorithm,
                    registered.Manifest.Signature.Value,
                    registered.OccurredAt)),
            PackageBuildRequested buildRequested => Create(
                BuildRequestedEventType,
                buildRequested.Key,
                buildRequested.OccurredAt,
                new BuildRequestedPayload(
                    buildRequested.Key.Name.Value,
                    buildRequested.Key.Version.ToString(),
                    (int)buildRequested.BuildPolicy.Isolation,
                    buildRequested.BuildPolicy.AllowNetwork,
                    buildRequested.BuildPolicy.Timeout.Ticks,
                    buildRequested.OccurredAt)),
            _ => throw new InvalidOperationException(
                $"Domain event type '{domainEvent.GetType().FullName}' is not supported by the PostgreSQL outbox.")
        };
    }

    public static string SerializePayload(object payload)
    {
        ArgumentNullException.ThrowIfNull(payload);
        return JsonSerializer.Serialize(payload, payload.GetType(), Options);
    }

    public static IReadOnlyList<IDomainEvent> Deserialize(string eventType, string payload)
    {
        ArgumentNullException.ThrowIfNull(eventType);
        ArgumentNullException.ThrowIfNull(payload);

        return eventType switch
        {
            RegisteredEventType => [DeserializeRegistered(payload)],
            BuildRequestedEventType => [DeserializeBuildRequested(payload)],
            _ => throw new InvalidOperationException(
                $"PostgreSQL outbox event type '{eventType}' is not supported.")
        };
    }

    private static PostgresOutboxEvent Create(
        string eventType,
        PackageKey key,
        DateTimeOffset occurredAt,
        object payload)
    {
        var idempotencyKey =
            $"{eventType}|{key.Name.Value}|{key.Version}|{occurredAt.UtcDateTime.Ticks}";

        return new PostgresOutboxEvent(
            idempotencyKey,
            key.Name.Value,
            key.Version.ToString(),
            eventType,
            SerializePayload(payload),
            occurredAt);
    }

    private static PackageRegistered DeserializeRegistered(string payload)
    {
        var value = JsonSerializer.Deserialize<RegisteredPayload>(payload, Options)
            ?? throw new InvalidOperationException(
                "PostgreSQL outbox PackageRegistered payload is invalid.");

        var manifest = PackageManifest.Create(
            PackageName.Create(value.Name),
            PackageVersion.Create(value.Version),
            PackageSource.Create(value.SourceUrl, value.SourceCommit),
            Toolchain.Create(value.ToolchainName, value.ToolchainVersion),
            ContentHash.Create(value.ContentHash),
            PackageProvenance.Create(value.Provenance),
            ToSignature(value.SignatureStatus, value.SignatureAlgorithm, value.SignatureValue));

        return new PackageRegistered(
            PackageKey.Create(value.Name, value.Version),
            manifest,
            value.OccurredAt);
    }

    private static PackageBuildRequested DeserializeBuildRequested(string payload)
    {
        var value = JsonSerializer.Deserialize<BuildRequestedPayload>(payload, Options)
            ?? throw new InvalidOperationException(
                "PostgreSQL outbox PackageBuildRequested payload is invalid.");

        var buildPolicy = BuildPolicy.Create(
            (BuildIsolation)value.BuildIsolation,
            value.AllowNetwork,
            TimeSpan.FromTicks(value.TimeoutTicks));

        return new PackageBuildRequested(
            PackageKey.Create(value.Name, value.Version),
            buildPolicy,
            value.OccurredAt);
    }

    private static PackageSignature ToSignature(
        int status,
        string? algorithm,
        string? value) =>
        (PackageSignatureStatus)status switch
        {
            PackageSignatureStatus.Unsigned => PackageSignature.Unsigned,
            PackageSignatureStatus.Pending => PackageSignature.Pending,
            PackageSignatureStatus.Signed => PackageSignature.Signed(algorithm, value),
            _ => throw new InvalidOperationException(
                $"PostgreSQL outbox signature status '{status}' is invalid.")
        };

    private sealed record RegisteredPayload(
        string Name,
        string Version,
        string SourceUrl,
        string SourceCommit,
        string ToolchainName,
        string ToolchainVersion,
        string ContentHash,
        string Provenance,
        int SignatureStatus,
        string? SignatureAlgorithm,
        string? SignatureValue,
        DateTimeOffset OccurredAt);

    private sealed record BuildRequestedPayload(
        string Name,
        string Version,
        int BuildIsolation,
        bool AllowNetwork,
        long TimeoutTicks,
        DateTimeOffset OccurredAt);
}
